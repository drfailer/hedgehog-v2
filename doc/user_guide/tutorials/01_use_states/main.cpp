#include <cstdio>
#include <algorithm>
#include <hedgehog/hedgehog.h>

// data ////////////////////////////////////////////////////////////////////////

struct Matrix {
    double *data = nullptr;
    size_t  rows = 0;
    size_t  cols = 0;

    Matrix(size_t rows, size_t cols): data(new double[rows * cols]), rows(rows), cols(cols) {}
    ~Matrix() { delete[] data; }

    double &operator()(size_t i, size_t j) { return data[i * cols + j]; }
    double const &operator()(size_t i, size_t j) const { return data[i * cols + j]; }
};

void print_matrix(Matrix const &m) {
    size_t rows = std::max(m.rows, 8UL);
    size_t cols = std::max(m.cols, 8UL);
    for (size_t i = 0; i < m.rows; ++i) {
        for (size_t j = 0; j < m.cols; ++j) {
            printf("%10.4lf  ", m(i, j));
        }
        printf("\n");
    }
}

struct MatrixTile {
    double *data   = nullptr;
    size_t  rows   = 0;
    size_t  cols   = 0;
    Matrix *matrix = nullptr;

    double &operator()(size_t i, size_t j) { return data[i * matrix->cols + j]; }
    double const &operator()(size_t i, size_t j) const { return data[i * matrix->cols + j]; }
};

struct TileOpData {
    MatrixTile A;
    MatrixTile B;
    MatrixTile C;
};

struct HadamardData {
    Matrix *A;
    Matrix *B;
    Matrix *C;
};

// graph ///////////////////////////////////////////////////////////////////////

//
// The split task receives the 3 matrix and split them into tiles (the tile
// size is configured when creating the task). It also sends the number of tiles
// to the result task.
//
struct SplitMatrixTask {
    using inputs  = hh::type_list<HadamardData>;
    using outputs = hh::type_list<size_t, TileOpData>;

    size_t tile_rows = 0;
    size_t tile_cols = 0;

    SplitMatrixTask(size_t tile_rows, size_t tile_cols)
        : tile_rows(tile_rows), tile_cols(tile_cols) {}

    void execute(auto ctx, auto data) {
        size_t nb_tiles_rows = data->C->rows / tile_rows + (data->C->rows % tile_rows == 0 ? 0 : 1);
        size_t nb_tiles_cols = data->C->cols / tile_cols + (data->C->cols % tile_cols == 0 ? 0 : 1);

        // notify the result state of the number of tiles
        ctx->push_result(hh::make_data<size_t>(nb_tiles_rows * nb_tiles_cols));

        // split the matrix and send the tiles to the hadamard task
        for (size_t ti = 0; ti < nb_tiles_rows; ++ti) {
            for (size_t tj = 0; tj < nb_tiles_cols; ++tj) {
                size_t i = ti * tile_rows;
                size_t j = tj * tile_cols;
                ctx->push_result(hh::make_data<TileOpData>(
                    MatrixTile{&data->A->operator()(i, j), std::min(data->A->rows - i, tile_rows), std::min(data->A->cols - j, tile_cols), data->A},
                    MatrixTile{&data->B->operator()(i, j), std::min(data->B->rows - i, tile_rows), std::min(data->B->cols - j, tile_cols), data->B},
                    MatrixTile{&data->C->operator()(i, j), std::min(data->C->rows - i, tile_rows), std::min(data->C->cols - j, tile_cols), data->C}
                ));
            }
        }
    }
};

//
// Receives matrix tiles and compute the hadamard product on them.
//
struct HadamardProductTask {
    using inputs  = hh::type_list<TileOpData>;
    using outputs = hh::type_list<TileOpData>;

    static void execute(auto ctx, auto data) {
        for (size_t i = 0; i < data->C.rows; ++i) {
            for (size_t j = 0; j < data->C.cols; ++j) {
                data->C(i, j) = data->A(i, j) * data->B(i, j);
            }
        }
        ctx->push_result(std::move(data));
    }
};

//
// Receives and counts the tiles. When all the tiles are received, the product
// is done and we send the result. Note that this organization only works when
// a single product is done at a time; multiplying tiles from different
// products in parallel will necessitate a different organization.
//
struct ResultState {
    using inputs  = hh::type_list<size_t, TileOpData>;
    using outputs = hh::type_list<HadamardData>;

    size_t tile_count = 0;
    size_t counter = 0;

    // receive the total number of tiles
    void execute(auto ctx, hh::data_t<size_t> data) {
        tile_count = *data;
    }

    // receive tiles
    void execute(auto ctx, hh::data_t<TileOpData> data) {
        ++counter;
        if (counter == tile_count) {
            ctx->push_result(hh::make_data<HadamardData>(data->A.matrix, data->B.matrix, data->C.matrix));
            counter = 0;
        }
    }
};

auto make_graph(size_t number_thread) {
    constexpr size_t TILE_ROWS = 2;
    constexpr size_t TILE_COLS = 2;

    auto graph = hh::make_graph<1, HadamardData, HadamardData>("Hadamard Graph");
    auto split_task = hh::make_task(std::make_shared<SplitMatrixTask>(TILE_ROWS, TILE_COLS), 1, "Split Task");
    auto hadamard_task = hh::make_task<HadamardProductTask>(number_thread, "Hadamard Task");
    auto result_state = hh::make_task<ResultState>(1, "Hadamard Task");

    graph->connect_inputs(split_task);
    graph->draw_edges(split_task, hadamard_task);       // only one type in common (TileOpData)
    graph->draw_edge<size_t>(split_task, result_state); // we only want to transfer the tile count (size_t)
    graph->draw_edges(hadamard_task, result_state);     // only one type in common (TileOpData)
    graph->connect_outputs(result_state);
    return graph;
}

// main ////////////////////////////////////////////////////////////////////////

int main(int, char **) {
    constexpr size_t MATRIX_ROWS = 4;
    constexpr size_t MATRIX_COLS = 4;

    Matrix A(MATRIX_ROWS, MATRIX_COLS);
    Matrix B(MATRIX_ROWS, MATRIX_COLS);
    Matrix C(MATRIX_ROWS, MATRIX_COLS);

    for (size_t i = 0; i < C.rows; ++i) {
        for (size_t j = 0; j < C.cols; ++j) {
            A(i, j) = 2;
            B(i, j) = 2;
            C(i, j) = 0;
        }
    }

    // building the graph
    auto graph = make_graph(4);

    // running the graph
    graph->start();

    // push the Hadamard data containing the matrix to multiply
    graph->push_data(hh::make_data<HadamardData>(&A, &B, &C));

    // get the results
    auto result = std::get<hh::data_t<HadamardData>>(graph->get_result());

    assert(result->A == &A);
    assert(result->B == &B);
    assert(result->C == &C);

    // print the matrix C
    printf("C =\n");
    print_matrix(*result->C);

    graph->stop();

    graph->generate_dot_file("graph.dot");

    return 0;
}
