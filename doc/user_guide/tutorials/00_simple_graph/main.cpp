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

// graph ///////////////////////////////////////////////////////////////////////

//
// Task that multiplies tiles.
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

auto make_graph(size_t number_thread) {
    auto graph = hh::make_graph<1, TileOpData, TileOpData>("Hadamard Graph");
    auto hadamard_task = hh::make_task<HadamardProductTask>(number_thread, "Hadamard Task");
    graph->connect_inputs(hadamard_task);
    graph->connect_outputs(hadamard_task);
    return graph;
}

// main ////////////////////////////////////////////////////////////////////////

int main(int, char **) {
    constexpr size_t MATRIX_ROWS = 4;
    constexpr size_t MATRIX_COLS = 4;
    constexpr size_t TILE_ROWS = 2;
    constexpr size_t TILE_COLS = 2;

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

    // push the tiles to multiply
    size_t NB_TILES_ROWS = MATRIX_ROWS / TILE_ROWS + (MATRIX_ROWS % TILE_ROWS == 0 ? 0 : 1);
    size_t NB_TILES_COLS = MATRIX_COLS / TILE_COLS + (MATRIX_COLS % TILE_COLS == 0 ? 0 : 1);
    for (size_t ti = 0; ti < NB_TILES_ROWS; ++ti) {
        for (size_t tj = 0; tj < NB_TILES_COLS; ++tj) {
            size_t i = ti * TILE_ROWS;
            size_t j = tj * TILE_COLS;
            graph->push_data(hh::make_data<TileOpData>(
                MatrixTile{&A(i, j), std::min(A.rows - i, TILE_ROWS), std::min(A.cols - j, TILE_COLS), &A},
                MatrixTile{&B(i, j), std::min(B.rows - i, TILE_ROWS), std::min(B.cols - j, TILE_COLS), &B},
                MatrixTile{&C(i, j), std::min(C.rows - i, TILE_ROWS), std::min(C.cols - j, TILE_COLS), &C}
            ));
        }
    }

    // eat the results
    graph->eat_results(NB_TILES_ROWS * NB_TILES_COLS);

    // print the matrix C
    printf("C =\n");
    print_matrix(C);

    graph->stop();

    graph->generate_dot_file("graph.dot");

    return 0;
}
