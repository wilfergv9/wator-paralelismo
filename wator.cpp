/*
 * Compilar: g++ -O2 -std=c++17 -o wator wator.cpp
 * Ejecutar:  ./wator
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <string>
#include <fstream>
#include <iomanip>
#include <thread>

// ─── Parámetros de simulación ────────────────────────────────────────────────
struct Config {
    int  N           = 50;    // filas
    int  M           = 50;    // columnas
    int  n_peces     = 200;   // peces iniciales
    int  n_tiburones = 50;    // tiburones iniciales
    int  fish_breed  = 4;     // pasos para reproducir pez
    int  shark_breed = 8;     // pasos para reproducir tiburón
    int  starve_time = 5;     // energía inicial del tiburón
    int  T           = 500;   // pasos de tiempo
    bool visualizar  = true;  // mostrar grilla en terminal
    int  delay_ms    = 80;    // ms entre frames
    bool guardar_csv = true;  // exportar poblaciones a CSV
};

// ─── Constantes de celda ─────────────────────────────────────────────────────
constexpr int VACIO   = 0;
constexpr int PEZ     = 1;
constexpr int TIBURON = 2;

// ─── Estructura de entidad ───────────────────────────────────────────────────
struct Entidad {
    int tipo;    // VACIO / PEZ / TIBURON
    int edad;    // pasos desde última reproducción
    int energia; // solo tiburones
};

// ─── RNG global ──────────────────────────────────────────────────────────────
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

template<typename T>
T& elegir_al_azar(std::vector<T>& v) {
    std::uniform_int_distribution<int> d(0, (int)v.size() - 1);
    return v[d(rng)];
}

// ─── Grilla ──────────────────────────────────────────────────────────────────
class Grilla {
public:
    int N, M;
    std::vector<std::vector<int>>     tipo;
    std::vector<std::vector<Entidad>> ent;

    Grilla(int N, int M)
        : N(N), M(M),
          tipo(N, std::vector<int>(M, VACIO)),
          ent (N, std::vector<Entidad>(M, {VACIO, 0, 0})) {}

    // Vecindad Von Neumann toroidal
    std::vector<std::pair<int,int>> vecinas(int r, int c) const {
        return {
            {(r-1+N)%N, c},
            {(r+1)%N,   c},
            {r, (c-1+M)%M},
            {r, (c+1)%M}
        };
    }

    std::vector<std::pair<int,int>> vecinas_de_tipo(int r, int c, int t) const {
        auto v = vecinas(r, c);
        std::vector<std::pair<int,int>> res;
        res.reserve(4);
        for (auto [nr, nc] : v)
            if (tipo[nr][nc] == t) res.push_back({nr, nc});
        return res;
    }

    void limpiar(int r, int c) {
        tipo[r][c] = VACIO;
        ent [r][c] = {VACIO, 0, 0};
    }

    void colocar(int r, int c, int t, int edad, int energia) {
        tipo[r][c]         = t;
        ent [r][c].tipo    = t;
        ent [r][c].edad    = edad;
        ent [r][c].energia = energia;
    }
};

// ─── Inicialización aleatoria ─────────────────────────────────────────────────
void inicializar(Grilla& g, const Config& cfg) {
    std::vector<std::pair<int,int>> celdas;
    celdas.reserve(g.N * g.M);
    for (int r = 0; r < g.N; r++)
        for (int c = 0; c < g.M; c++)
            celdas.push_back({r, c});
    std::shuffle(celdas.begin(), celdas.end(), rng);

    int idx = 0;
    for (int i = 0; i < cfg.n_peces && idx < (int)celdas.size(); i++, idx++) {
        auto [r, c] = celdas[idx];
        g.colocar(r, c, PEZ, 0, 0);
    }
    for (int i = 0; i < cfg.n_tiburones && idx < (int)celdas.size(); i++, idx++) {
        auto [r, c] = celdas[idx];
        g.colocar(r, c, TIBURON, 0, cfg.starve_time);
    }
}

void paso(Grilla& g, const Config& cfg) {

    // ── Fase peces ────────────────────────────────────────────────────────────
    {
        std::vector<std::pair<int,int>> peces;
        peces.reserve(g.N * g.M / 4);
        for (int r = 0; r < g.N; r++)
            for (int c = 0; c < g.M; c++)
                if (g.tipo[r][c] == PEZ) peces.push_back({r, c});
        std::shuffle(peces.begin(), peces.end(), rng);

        // Evitar procesar dos veces un pez que se mueve a una celda no visitada
        std::vector<std::vector<bool>> ya_movido(g.N, std::vector<bool>(g.M, false));

        for (auto [r, c] : peces) {
            if (ya_movido[r][c])     continue; // llegó aquí este paso
            if (g.tipo[r][c] != PEZ) continue; // fue comido por un tiburón previo

            auto vacias = g.vecinas_de_tipo(r, c, VACIO);

            // Si no hay vecinas vacías: no se mueve, edad no cambia
            if (vacias.empty()) continue;

            auto [nr, nc] = elegir_al_azar(vacias);

            // Incrementar edad ANTES de decidir reproducción
            int nueva_edad = g.ent[r][c].edad + 1;
            bool reproduce = (nueva_edad >= cfg.fish_breed);

            // Mover pez al destino
            g.colocar(nr, nc, PEZ, reproduce ? 0 : nueva_edad, 0);
            ya_movido[nr][nc] = true;

            // Origen: cría (edad 0) o vacío
            if (reproduce)
                g.colocar(r, c, PEZ, 0, 0);
            else
                g.limpiar(r, c);
        }
    }

    // ── Fase tiburones ────────────────────────────────────────────────────────
    {
        std::vector<std::pair<int,int>> tiburones;
        tiburones.reserve(g.N * g.M / 8);
        for (int r = 0; r < g.N; r++)
            for (int c = 0; c < g.M; c++)
                if (g.tipo[r][c] == TIBURON) tiburones.push_back({r, c});
        std::shuffle(tiburones.begin(), tiburones.end(), rng);

        std::vector<std::vector<bool>> ya_movido(g.N, std::vector<bool>(g.M, false));

        for (auto [r, c] : tiburones) {
            if (ya_movido[r][c])          continue;
            if (g.tipo[r][c] != TIBURON)  continue;

            int edad    = g.ent[r][c].edad;
            int energia = g.ent[r][c].energia;
            int nr = r, nc = c;

            auto con_pez = g.vecinas_de_tipo(r, c, PEZ);

            if (!con_pez.empty()) {
                // Comer: moverse a celda del pez, restaurar energía
                auto [pr, pc] = elegir_al_azar(con_pez);
                nr = pr; nc = pc;
                energia = cfg.starve_time;   // energía se reinicia al comer
            } else {
                // No hay peces: intentar moverse a vacío
                auto vacias = g.vecinas_de_tipo(r, c, VACIO);
                if (!vacias.empty()) {
                    auto [vr, vc] = elegir_al_azar(vacias);
                    nr = vr; nc = vc;
                }
                energia -= 1;  // pierde energía SOLO si no comió
            }

            // Incrementar edad (siempre)
            edad += 1;

            // Morir por hambre (ANTES de reproducirse)
            if (energia <= 0) {
                g.limpiar(r, c);
                continue;
            }

            // Reproducción
            bool reproduce = (edad >= cfg.shark_breed);

            if (nr == r && nc == c) {
                // No se pudo mover
                g.colocar(r, c, TIBURON, reproduce ? 0 : edad, energia);
                continue;
            }

            // Mover tiburón al destino
            g.colocar(nr, nc, TIBURON, reproduce ? 0 : edad, energia);
            ya_movido[nr][nc] = true;

            // Origen: cría o vacío
            if (reproduce)
                g.colocar(r, c, TIBURON, 0, cfg.starve_time);
            else
                g.limpiar(r, c);
        }
    }
}

// ─── Contadores ───────────────────────────────────────────────────────────────
std::pair<int,int> contar(const Grilla& g) {
    int p = 0, s = 0;
    for (int r = 0; r < g.N; r++)
        for (int c = 0; c < g.M; c++) {
            if (g.tipo[r][c] == PEZ)     p++;
            if (g.tipo[r][c] == TIBURON) s++;
        }
    return {p, s};
}

// ─── Visualización ────────────────────────────────────────────────────────────
void limpiar_pantalla() {
#ifdef _WIN32
    system("cls");
#else
    std::cout << "\033[2J\033[H";
#endif
}

void grafico_ascii(const std::vector<int>& hp, const std::vector<int>& hs) {
    if (hp.empty()) return;
    int max_val = 1;
    for (int v : hp) max_val = std::max(max_val, v);
    for (int v : hs) max_val = std::max(max_val, v);

    const int ALTO  = 8;
    const int ANCHO = std::min((int)hp.size(), 80);
    int start = (int)hp.size() - ANCHO;

    std::cout << "\n\033[1;37m  Historial (últimos " << ANCHO << " pasos)\033[0m\n";
    for (int row = ALTO; row >= 1; row--) {
        std::cout << "  ";
        for (int i = start; i < (int)hp.size(); i++) {
            double p = (double)hp[i] / max_val * ALTO;
            double s = (double)hs[i] / max_val * ALTO;
            if      (s >= row) std::cout << "\033[91m█\033[0m";
            else if (p >= row) std::cout << "\033[96m█\033[0m";
            else               std::cout << "\033[90m░\033[0m";
        }
        std::cout << "\n";
    }
    std::cout << "  ";
    for (int i = 0; i < ANCHO; i++) std::cout << "─";
    std::cout << "\n";
}

void visualizar(const Grilla& g, int t, int peces, int tiburones,
                const std::vector<int>& hp, const std::vector<int>& hs) {
    limpiar_pantalla();

    std::cout << "\033[1;37m╔══════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1;37m║        🌊  WA-TOR SIMULATION         ║\033[0m\n";
    std::cout << "\033[1;37m╚══════════════════════════════════════╝\033[0m\n";
    std::cout << "  Paso: \033[1;33m" << std::setw(4) << t << "\033[0m"
              << "   🐟 Peces: \033[96m" << std::setw(5) << peces << "\033[0m"
              << "   🦈 Tiburones: \033[91m" << std::setw(4) << tiburones << "\033[0m\n\n";

    int maxR = std::min(g.N, 30);
    int maxC = std::min(g.M, 60);

    for (int r = 0; r < maxR; r++) {
        std::cout << "  ";
        for (int c = 0; c < maxC; c++) {
            switch (g.tipo[r][c]) {
                case PEZ:     std::cout << "\033[96m●\033[0m"; break;
                case TIBURON: std::cout << "\033[91m▲\033[0m"; break;
                default:      std::cout << "\033[90m·\033[0m"; break;
            }
        }
        std::cout << "\n";
    }
    if (g.N > 30 || g.M > 60)
        std::cout << "  \033[90m[Mostrando " << maxR << "x" << maxC
                  << " de " << g.N << "x" << g.M << "]\033[0m\n";

    std::cout << "\n  \033[96m●\033[0m Peces   \033[91m▲\033[0m Tiburones   \033[90m·\033[0m Vacío\n";

    if ((int)hp.size() >= 5) grafico_ascii(hp, hs);
}

// ─── Guardar CSV ──────────────────────────────────────────────────────────────
void guardar_csv(const std::vector<int>& hp, const std::vector<int>& ht,
                 const std::string& nombre = "wator_poblacion.csv") {
    std::ofstream f(nombre);
    f << "t,peces,tiburones\n";
    for (int i = 0; i < (int)hp.size(); i++)
        f << (i+1) << "," << hp[i] << "," << ht[i] << "\n";
    std::cout << "\n  ✅ CSV guardado en: " << nombre << "\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    Config cfg;

    std::cout << "\033[1;37m╔══════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1;37m║       WA-TOR  —  Configuración           ║\033[0m\n";
    std::cout << "\033[1;37m╚══════════════════════════════════════════╝\033[0m\n";
    std::cout << "\n  Usar configuración por defecto? (s/n): ";
    char resp;
    std::cin >> resp;

    if (resp == 'n' || resp == 'N') {
        std::cout << "  Filas (N)                     [" << cfg.N           << "]: "; std::cin >> cfg.N;
        std::cout << "  Columnas (M)                  [" << cfg.M           << "]: "; std::cin >> cfg.M;
        std::cout << "  Peces iniciales               [" << cfg.n_peces     << "]: "; std::cin >> cfg.n_peces;
        std::cout << "  Tiburones iniciales           [" << cfg.n_tiburones << "]: "; std::cin >> cfg.n_tiburones;
        std::cout << "  fish_breed                    [" << cfg.fish_breed  << "]: "; std::cin >> cfg.fish_breed;
        std::cout << "  shark_breed                   [" << cfg.shark_breed << "]: "; std::cin >> cfg.shark_breed;
        std::cout << "  starve_time (energía inicial) [" << cfg.starve_time << "]: "; std::cin >> cfg.starve_time;
        std::cout << "  Pasos de tiempo T             [" << cfg.T           << "]: "; std::cin >> cfg.T;
        std::cout << "  Visualizar en terminal (1/0)  [" << cfg.visualizar  << "]: "; std::cin >> cfg.visualizar;
        if (cfg.visualizar) {
            std::cout << "  Delay entre frames (ms)       [" << cfg.delay_ms << "]: "; std::cin >> cfg.delay_ms;
        }
        std::cout << "  Guardar CSV (1/0)             [" << cfg.guardar_csv << "]: "; std::cin >> cfg.guardar_csv;
    }

    int total = cfg.N * cfg.M;
    cfg.n_peces     = std::min(cfg.n_peces,     total);
    cfg.n_tiburones = std::min(cfg.n_tiburones, total - cfg.n_peces);

    Grilla g(cfg.N, cfg.M);
    inicializar(g, cfg);

    std::vector<int> hist_peces, hist_tiburones;
    hist_peces.reserve(cfg.T);
    hist_tiburones.reserve(cfg.T);

    if (!cfg.visualizar)
        std::cout << "\n  Iniciando simulación (" << cfg.T << " pasos)...\n\n";
    else {
        std::cout << "\n  Iniciando simulación...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    for (int t = 1; t <= cfg.T; t++) {
        paso(g, cfg);

        auto [np, nt] = contar(g);
        hist_peces.push_back(np);
        hist_tiburones.push_back(nt);

        if (cfg.visualizar) {
            visualizar(g, t, np, nt, hist_peces, hist_tiburones);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.delay_ms));
        } else if (t % 50 == 0 || t == 1) {
            std::cout << "  t=" << std::setw(5) << t
                      << "  Peces=" << std::setw(5) << np
                      << "  Tiburones=" << std::setw(4) << nt << "\n";
        }

        if (np == 0 && nt == 0) {
            std::cout << "\n  ⚠️  Extinción total en t=" << t << "\n";
            break;
        }
    }

    if (!cfg.visualizar) {
        std::cout << "\n";
        grafico_ascii(hist_peces, hist_tiburones);
    }

    auto [fp, ft] = contar(g);
    std::cout << "\n\033[1;37m  ─── Resumen Final ───\033[0m\n";
    std::cout << "  Peces finales:     \033[96m" << fp << "\033[0m\n";
    std::cout << "  Tiburones finales: \033[91m" << ft << "\033[0m\n";

    if (cfg.guardar_csv)
        guardar_csv(hist_peces, hist_tiburones);

    std::cout << "\n  Presiona Enter para salir...\n";
    std::cin.ignore();
    std::cin.get();
    return 0;
}
