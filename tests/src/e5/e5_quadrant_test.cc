#include <cassert>
#include <cmath>
#include <iostream>

#include "topology.hh"

int main() {
  // Configuração da topologia: 2 colunas x 3 linhas, alcance RSU = 10
  Topology::Size size{ 2, 3 };
  double rsu_range = 10.0;
  Topology topo(size, rsu_range);

  // Verifica getters mínimos (size e range)
  auto sz = topo.get_size();
  assert(sz.first == size.first && sz.second == size.second);
  std::cout << "Size OK: (" << sz.first << "," << sz.second << ")" << std::endl;
  assert(std::abs(topo.get_range() - rsu_range) < 1e-9);
  std::cout << "Range OK: " << topo.get_range() << std::endl;

  // === Teste de pontos interiores ===\n";
  int cols = sz.first;
  int rows = sz.second;
  for (int qy = 0; qy < rows; ++qy) {
    for (int qx = 0; qx < cols; ++qx) {
      double x = (((double)qx - cols / 2.0 + 0.5)) * 2 * rsu_range;
      double y = -(((double)qy - rows / 2.0 + 0.5)) * 2 * rsu_range;
      int id = topo.get_quadrant_id({ x, y });
      int expected = qx + qy * cols;
      std::cout << "Interior cell (" << qx << "," << qy << ") -> ponto(" << x
                << "," << y << ") => id=" << id << ", esperado=" << expected
                << "\n";
      assert(id == expected);
    }
  }

  // 2) Teste em fronteira vertical entre colunas (escolhe o da direita)
  std::cout << "\n=== Teste de fronteiras verticais ===\n";
  for (int qy = 0; qy < rows; ++qy) {
    for (int k = 1; k < cols; ++k) {
      double x = ((double)k - cols / 2.0) * 2 * rsu_range;
      double y = -((double)qy - rows / 2.0 + 0.5) * 2 * rsu_range;
      int id = topo.get_quadrant_id({ x, y });
      int id_right = k + qy * cols;
      std::cout << "Fronteira vertical x=" << x << ", y=" << y
                << " -> id=" << id << ", esperado=" << id_right << "\n";
      assert(id == id_right);
    }
  }

  // 3) Teste em fronteira horizontal entre linhas (escolhe o de baixo)
  std::cout << "\n=== Teste de fronteiras horizontais ===\n";
  for (int qx = 0; qx < cols; ++qx) {
    for (int k = 1; k < rows; ++k) {
      double x = ((double)qx - cols / 2.0 + 0.5) * 2 * rsu_range;
      double y = -((double)k - rows / 2.0) * 2 * rsu_range;
      int id = topo.get_quadrant_id({ x, y });
      int id_top = qx + k * cols;
      std::cout << "Fronteira horizontal x=" << x << ", y=" << y
                << " -> id=" << id << ", esperado=" << id_top << "\n";
      assert(id == id_top);
    }
  }

  // 4) Teste de interseção de fronteiras (ponto no meio de 4 quadrantes)
  std::cout << "\n=== Teste de interseções de fronteiras ===\n";
  for (int k = 1; k < rows; ++k) {
    int kv = 1; // única fronteira vertical para cols=2
    double x = ((double)kv - cols / 2.0) * 2 * rsu_range;
    double y = -((double)k - rows / 2.0) * 2 * rsu_range;
    int id = topo.get_quadrant_id({ x, y });
    int expected = kv + k * cols; // quadrante à direita e acima
    std::cout << "Interseção frontieras kv=" << kv << ", k=" << k
              << " -> ponto(" << x << "," << y << ") => id=" << id
              << ", esperado=" << expected << "\n";
    assert(id == expected);
  }

  std::cout << "\nTodos os testes passaram com sucesso.\n";
  return 0;
}
