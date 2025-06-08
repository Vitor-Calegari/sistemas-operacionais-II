#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "topology.hh"

int main() {
    // Configuração da topologia: 3 colunas x 4 linhas, alcance RSU = 10
    Topology::Size size{3, 4};
    double rsu_range = 10.0;
    Topology topo(size, rsu_range);

    // Verifica getters
    auto sz = topo.get_size();
    assert(sz.first == size.first && sz.second == size.second);
    auto dim = topo.get_dimension();
    assert(std::abs(dim.first - (size.first * rsu_range)) < 1e-9);
    assert(std::abs(dim.second - (size.second * rsu_range)) < 1e-9);
    std::cout << "Size OK: (" << sz.first << "," << sz.second << ")\n"
              << "Dimension OK: (" << dim.first << "," << dim.second << ")\n"
              << "Range OK: " << topo.get_range() << "\n\n";

    int cols = size.first;
    int rows = size.second;

    // 1) Teste de ponto interior para cada quadrante
    std::cout << "=== Teste de pontos interiores ===\n";
    for (int qy = 0; qy < rows; ++qy) {
        for (int qx = 0; qx < cols; ++qx) {
            double x = ((double)qx - cols/2.0 + 0.5) * 2 * rsu_range;
            double y = ((double)qy + rows/2.0 + 0.5) * 2 * rsu_range;
            int id = topo.get_quadrant_id({x, y});
            int expected = qx + qy * rows;
            std::cout << "Interior cell (" << qx << "," << qy
                      << ") -> ponto(" << x << "," << y
                      << ") => id=" << id
                      << ", esperado=" << expected << "\n";
            assert(id == expected);
        }
    }

    // 2) Teste em fronteira vertical entre colunas (escolhe o da direita)
    std::cout << "\n=== Teste de fronteiras verticais ===\n";
    for (int qy = 0; qy < rows; ++qy) {
        for (int k = 1; k < cols; ++k) {
            double x = ((double)k - cols/2.0) * 2 * rsu_range;
            double y = ((double)qy + rows/2.0 + 0.5) * 2 * rsu_range;
            int id = topo.get_quadrant_id({x, y});
            int id_right = k + qy * rows;
            std::cout << "Fronteira vertical x=" << x << ", y=" << y
                      << " -> id=" << id
                      << ", esperado=" << id_right << "\n";
            assert(id == id_right);
        }
    }

    // 3) Teste em fronteira horizontal entre linhas (escolhe o de cima)
    std::cout << "\n=== Teste de fronteiras horizontais ===\n";
    for (int qx = 0; qx < cols; ++qx) {
        for (int k = 1; k < rows; ++k) {
            double x = ((double)qx - cols/2.0 + 0.5) * 2 * rsu_range;
            double y = ((double)k + rows/2.0) * 2 * rsu_range;
            int id = topo.get_quadrant_id({x, y});
            int id_top = qx + k * rows;
            std::cout << "Fronteira horizontal x=" << x << ", y=" << y
                      << " -> id=" << id
                      << ", esperado=" << id_top << "\n";
            assert(id == id_top);
        }
    }

    std::cout << "\nTodos os testes passaram com sucesso.\n";
    return 0;
}

// g++ -std=c++20 -Iinclude tests/src/e5/e5_quadrant_test.cc -o topology_test
