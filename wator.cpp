/*
 * Wa-Tor Simulation — Paralelizado con OpenMP
 * ============================================
 * Estrategia: Coloreo de tablero (checkerboard / 4-coloring)
 *
 *   Las celdas se dividen en 4 grupos según (r%2, c%2):
 *     Grupo 0: (par,   par  )
 *     Grupo 1: (par,   impar)
 *     Grupo 2: (impar, par  )
 *     Grupo 3: (impar, impar)
 *
 *   Ninguna celda de un grupo es vecina Von Neumann de otra del mismo grupo,
 *   por lo que las celdas dentro de un grupo son INDEPENDIENTES entre sí
 *   y se pueden procesar en paralelo sin race conditions.
 *   Los 4 grupos se procesan secuencialmente uno tras otro.
 *
 *   Otros puntos paralelos:
 *     - Recolección de entidades por filas (#pragma omp parallel for)
 *     - Conteo con reduction(+:p) reduction(+:s)
 *     - Cada hilo tiene su propio mt19937 (thread_local) para el RNG
 *
 * Compilar: g++ -O2 -std=c++17 -fopenmp -o wator wator.cpp
 * Ejecutar: ./wator
 *           OMP_NUM_THREADS=8 ./wator   (para fijar número de hilos)
 */

#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>
#include <string>
#include <fstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <omp.h>

// ─── Parámetros de simulación ────────────────────────────────────────────────
struct Config {
    int  N           = 50;
    int  M           = 50;
    int  n_peces     = 200;
    int  n_tiburones = 50;
    int  fish_breed  = 4;
    int  shark_breed = 8;
    int  starve_time = 5;
    int  T           = 500;
    bool visualizar  = true;
    int  delay_ms    = 80;
    bool guardar_csv = true;
    int  n_hilos     = 0;    // 0 = dejar que OpenMP decida
};

// ─── Constantes de celda ─────────────────────────────────────────────────────
constexpr int VACIO   = 0;
constexpr int PEZ     = 1;
constexpr int TIBURON = 2;

// ─── Estructura de entidad ───────────────────────────────────────────────────
struct Entidad {
    int tipo;
    int edad;
    int energia;
};

// ─── RNG por hilo (thread_local) ─────────────────────────────────────────────
// Cada hilo OpenMP tiene su propio generador, inicializado con una semilla
// distinta para evitar correlaciones entre hilos.
thread_local std::mt19937 tl_rng;

void inicializar_rngs() {
    // Se llama dentro de una región paralela para que cada hilo inicialice el suyo
    #pragma omp parallel
    {
        auto seed = std::chrono::steady_clock::now().time_since_epoch().count()
                    ^ (static_cast<uint64_t>(omp_get_thread_num()) * 6364136223846793005ULL);
        tl_rng.seed(seed);
    }
}

template<typename T>
T& elegir_al_azar(std::vector<T>& v) {
    std::uniform_int_distribution<int> d(0, (int)v.size() - 1);
    return v[d(tl_rng)];
}

// ─── Grilla ──────────────────────────────────────────────────────────────────
class Grilla {
public:
    int N, M;
    // Almacenamiento plano (row-major) para mejor localidad de caché
    std::vector<int>     tipo;
    std::vector<Entidad> ent;

    Grilla(int N, int M)
        : N(N), M(M),
          tipo(N * M, VACIO),
          ent (N * M, {VACIO, 0, 0}) {}

    inline int idx(int r, int c) const { return r * M + c; }

    // Vecindad Von Neumann toroidal — devuelve índices planos
    std::array<int,4> vecinas(int r, int c) const {
        return {
            ((r-1+N)%N) * M + c,
            ((r+1)%N)   * M + c,
            r * M + (c-1+M)%M,
            r * M + (c+1)%M
        };
    }

    // Vecinas de un tipo concreto (devuelve índices planos)
    std::vector<int> vecinas_de_tipo(int r, int c, int t) const {
        std::vector<int> res;
        res.reserve(4);
        for (int i : vecinas(r, c))
            if (tipo[i] == t) res.push_back(i);
        return res;
    }

    void limpiar(int i) {
        tipo[i] = VACIO;
        ent [i] = {VACIO, 0, 0};
    }

    void colocar(int i, int t, int edad, int energia) {
        tipo[i]         = t;
        ent [i].tipo    = t;
        ent [i].edad    = edad;
        ent [i].energia = energia;
    }
};

// ─── Inicialización ───────────────────────────────────────────────────────────
void inicializar(Grilla& g, const Config& cfg) {
    // Secuencial: solo ocurre una vez
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::vector<int> celdas(g.N * g.M);
    std::iota(celdas.begin(), celdas.end(), 0);
    std::shuffle(celdas.begin(), celdas.end(), rng);

    int idx = 0;
    for (int i = 0; i < cfg.n_peces && idx < (int)celdas.size(); i++, idx++)
        g.colocar(celdas[idx], PEZ, 0, 0);
    for (int i = 0; i < cfg.n_tiburones && idx < (int)celdas.size(); i++, idx++)
        g.colocar(celdas[idx], TIBURON, 0, cfg.starve_time);
}


void paso(Grilla& g, const Config& cfg) {

    const int NM = g.N * g.M;

    // ── Fase peces ────────────────────────────────────────────────────────────
    for (int color = 0; color < 4; color++) {
        int dr = color >> 1;   // 0 o 1
        int dc = color &  1;   // 0 o 1

        // Recolectar peces de este color en paralelo por filas
        // Cada hilo llena su propio buffer local, luego se concatenan
        std::vector<std::vector<int>> bufs(omp_get_max_threads());

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& buf = bufs[tid];

            #pragma omp for schedule(static) nowait
            for (int r = dr; r < g.N; r += 2) {
                for (int c = dc; c < g.M; c += 2) {
                    int i = g.idx(r, c);
                    if (g.tipo[i] == PEZ) buf.push_back(i);
                }
            }
        }

        // Concatenar buffers
        std::vector<int> peces;
        for (auto& b : bufs) {
            peces.insert(peces.end(), b.begin(), b.end());
            b.clear();
        }

        // Mezclar (secuencial — shuffle paralelo no preserva uniformidad fácilmente)
        std::shuffle(peces.begin(), peces.end(), tl_rng);

        // Procesar en paralelo — sin race conditions por el coloring
        #pragma omp parallel for schedule(dynamic, 64)
        for (int k = 0; k < (int)peces.size(); k++) {
            int i = peces[k];
            int r = i / g.M;
            int c = i % g.M;

            if (g.tipo[i] != PEZ) continue; // fue sobreescrito (raro en mismo color)

            auto vacias = g.vecinas_de_tipo(r, c, VACIO);
            if (vacias.empty()) continue;

            int dest      = elegir_al_azar(vacias);
            int nueva_edad = g.ent[i].edad + 1;
            bool reproduce = (nueva_edad >= cfg.fish_breed);

            g.colocar(dest, PEZ, reproduce ? 0 : nueva_edad, 0);

            if (reproduce)
                g.colocar(i, PEZ, 0, 0);
            else
                g.limpiar(i);
        }
    } // for color (peces)

    // ── Fase tiburones ────────────────────────────────────────────────────────
    for (int color = 0; color < 4; color++) {
        int dr = color >> 1;
        int dc = color &  1;

        std::vector<std::vector<int>> bufs(omp_get_max_threads());

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& buf = bufs[tid];

            #pragma omp for schedule(static) nowait
            for (int r = dr; r < g.N; r += 2) {
                for (int c = dc; c < g.M; c += 2) {
                    int i = g.idx(r, c);
                    if (g.tipo[i] == TIBURON) buf.push_back(i);
                }
            }
        }

        std::vector<int> tiburones;
        for (auto& b : bufs) {
            tiburones.insert(tiburones.end(), b.begin(), b.end());
            b.clear();
        }

        std::shuffle(tiburones.begin(), tiburones.end(), tl_rng);

        #pragma omp parallel for schedule(dynamic, 64)
        for (int k = 0; k < (int)tiburones.size(); k++) {
            int i = tiburones[k];
            int r = i / g.M;
            int c = i % g.M;

            if (g.tipo[i] != TIBURON) continue;

            int edad    = g.ent[i].edad;
            int energia = g.ent[i].energia;
            int dest    = i;

            auto con_pez = g.vecinas_de_tipo(r, c, PEZ);

            if (!con_pez.empty()) {
                dest    = elegir_al_azar(con_pez);
                energia = cfg.starve_time;          // reiniciar al comer
            } else {
                auto vacias = g.vecinas_de_tipo(r, c, VACIO);
                if (!vacias.empty())
                    dest = elegir_al_azar(vacias);
                energia -= 1;                       // pierde energía solo si no comió
            }

            edad += 1;

            // Morir por hambre
            if (energia <= 0) {
                g.limpiar(i);
                continue;
            }

            bool reproduce = (edad >= cfg.shark_breed);

            if (dest == i) {
                // No se pudo mover
                g.colocar(i, TIBURON, reproduce ? 0 : edad, energia);
                continue;
            }

            g.colocar(dest, TIBURON, reproduce ? 0 : edad, energia);

            if (reproduce)
                g.colocar(i, TIBURON, 0, cfg.starve_time);
            else
                g.limpiar(i);
        }
    } // for color (tiburones)
}

// ─── Conteo paralelo con reduction ───────────────────────────────────────────
std::pair<int,int> contar(const Grilla& g) {
    int p = 0, s = 0;
    const int NM = g.N * g.M;

    #pragma omp parallel for reduction(+:p) reduction(+:s) schedule(static)
    for (int i = 0; i < NM; i++) {
        if (g.tipo[i] == PEZ)     p++;
        if (g.tipo[i] == TIBURON) s++;
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
                const std::vector<int>& hp, const std::vector<int>& hs,
                int n_hilos) {
    limpiar_pantalla();
    std::cout << "\033[1;37m╔══════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1;37m║          🌊  WA-TOR  (OpenMP)            ║\033[0m\n";
    std::cout << "\033[1;37m╚══════════════════════════════════════════╝\033[0m\n";
    std::cout << "  Paso: \033[1;33m" << std::setw(4) << t << "\033[0m"
              << "   🐟 Peces: \033[96m" << std::setw(5) << peces << "\033[0m"
              << "   🦈 Tiburones: \033[91m" << std::setw(4) << tiburones << "\033[0m"
              << "   ⚙️  Hilos: \033[93m" << n_hilos << "\033[0m\n\n";

    int maxR = std::min(g.N, 30);
    int maxC = std::min(g.M, 60);
    for (int r = 0; r < maxR; r++) {
        std::cout << "  ";
        for (int c = 0; c < maxC; c++) {
            switch (g.tipo[g.idx(r,c)]) {
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
        std::cout << "  Núm. hilos OpenMP (0=auto)    [" << cfg.n_hilos     << "]: "; std::cin >> cfg.n_hilos;
        std::cout << "  Visualizar en terminal (1/0)  [" << cfg.visualizar  << "]: "; std::cin >> cfg.visualizar;
        if (cfg.visualizar) {
            std::cout << "  Delay entre frames (ms)       [" << cfg.delay_ms << "]: "; std::cin >> cfg.delay_ms;
        }
        std::cout << "  Guardar CSV (1/0)             [" << cfg.guardar_csv << "]: "; std::cin >> cfg.guardar_csv;
    }

    // Configurar número de hilos
    if (cfg.n_hilos > 0) omp_set_num_threads(cfg.n_hilos);
    int n_hilos_real = 0;
    #pragma omp parallel
    {
        #pragma omp single
        n_hilos_real = omp_get_num_threads();
    }

    // Inicializar RNG de cada hilo
    inicializar_rngs();

    int total = cfg.N * cfg.M;
    cfg.n_peces     = std::min(cfg.n_peces,     total);
    cfg.n_tiburones = std::min(cfg.n_tiburones, total - cfg.n_peces);

    Grilla g(cfg.N, cfg.M);
    inicializar(g, cfg);

    std::cout << "\n  Hilos OpenMP activos: \033[93m" << n_hilos_real << "\033[0m\n";

    std::vector<int> hist_peces, hist_tiburones;
    hist_peces.reserve(cfg.T);
    hist_tiburones.reserve(cfg.T);

    if (!cfg.visualizar)
        std::cout << "  Iniciando simulación (" << cfg.T << " pasos)...\n\n";
    else {
        std::cout << "  Iniciando simulación...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    auto t_inicio = std::chrono::steady_clock::now();

    for (int t = 1; t <= cfg.T; t++) {
        paso(g, cfg);

        auto [np, nt] = contar(g);
        hist_peces.push_back(np);
        hist_tiburones.push_back(nt);

        if (cfg.visualizar) {
            visualizar(g, t, np, nt, hist_peces, hist_tiburones, n_hilos_real);
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

    auto t_fin = std::chrono::steady_clock::now();
    double seg = std::chrono::duration<double>(t_fin - t_inicio).count();

    if (!cfg.visualizar) {
        std::cout << "\n";
        grafico_ascii(hist_peces, hist_tiburones);
    }

    auto [fp, ft] = contar(g);
    std::cout << "\n\033[1;37m  ─── Resumen Final ───\033[0m\n";
    std::cout << "  Peces finales:     \033[96m" << fp << "\033[0m\n";
    std::cout << "  Tiburones finales: \033[91m" << ft << "\033[0m\n";
    std::cout << "  Tiempo total:      \033[93m" << std::fixed << std::setprecision(3)
              << seg << "s\033[0m  ("
              << std::setprecision(1) << (hist_peces.size() / seg)
              << " pasos/s con " << n_hilos_real << " hilos)\n";

    if (cfg.guardar_csv)
        guardar_csv(hist_peces, hist_tiburones);

    std::cout << "\n  Presiona Enter para salir...\n";
    std::cin.ignore();
    std::cin.get();
    return 0;
}