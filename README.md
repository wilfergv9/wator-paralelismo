# Wa-Tor Simulation — Paralelizado con OpenMP

Una simulación del modelo **Wa-Tor** (depredadores y presas en un océano) implementada en **C++ con OpenMP** para paralelización eficiente.

## 📋 Descripción

El modelo Wa-Tor simula un ecosistema bidimensional donde:
- 🐟 **Peces** se mueven aleatoriamente y se reproducen periódicamente
- 🦈 **Tiburones** cazan peces para sobrevivir y se reproducen después de cierto tiempo
- Ambos mueren si no logran sus objetivos (reproducción/alimentación)

### Estrategia de Paralelización

Se utiliza **coloreado de tablero** (4-coloring checkerboard):
- Las celdas se dividen en 4 grupos según `(fila%2, columna%2)`
- Ninguna celda de un grupo es vecina Von Neumann de otra del mismo grupo
- Cada grupo se procesa en paralelo sin race conditions
- Los 4 grupos se procesan secuencialmente

## 🚀 Compilación

```bash
g++ -O2 -std=c++17 -fopenmp -o wator wator.cpp
```

**Requisitos:**
- GCC con soporte OpenMP
- C++17

## 🏃 Ejecución

### Básico
```bash
./wator
```

### Con control de hilos OpenMP
```bash
OMP_NUM_THREADS=8 ./wator
```

### Parámetros personalizados
Edita la estructura `Config` en `wator.cpp` para ajustar:
- `N`, `M` - dimensiones del tablero (default: 50×50)
- `n_peces` - población inicial de peces
- `n_tiburones` - población inicial de tiburones
- `fish_breed` - ciclos para que un pez se reproduzca
- `shark_breed` - ciclos para que un tiburón se reproduzca
- `starve_time` - ciclos antes de que un tiburón muera de hambre
- `T` - número total de iteraciones/pasos de simulación
- `visualizar` - mostrar grid en cada paso (true/false)

## 📊 Análisis de Resultados

Después de ejecutar la simulación, visualiza los resultados:

```bash
python wator_plot.py wator_poblacion.csv
```

**Características del análisis:**
- Gráfico de población vs tiempo (peces y tiburones)
- Análisis de ciclos y estabilidad
- Detección automática de picos poblacionales
- Estadísticas de rendimiento OpenMP

### Comparar múltiples ejecuciones
```bash
python wator_plot.py run1.csv run2.csv run3.csv
```

## 📁 Archivos

| Archivo | Descripción |
|---------|-------------|
| `wator.cpp` | Programa principal (simulación) |
| `wator_plot.py` | Script de visualización y análisis |
| `wator_poblacion.csv` | Datos de población generados por la simulación |
| `wator_resultado.png` | Gráficos de salida |

## 📈 Salida

La simulación genera:
1. **`wator_poblacion.csv`** - evolución de poblaciones por paso:
   ```
   paso,peces,tiburones,tiempo_paso_ms
   0,200,50,0.45
   1,198,51,0.42
   ...
   ```

2. **`wator_resultado.png`** - visualización de resultados

3. **Consola** - información de rendimiento y ejecución

## 🔧 Ejemplo de uso completo

```bash
# Compilar
g++ -O2 -std=c++17 -fopenmp -o wator wator.cpp

# Ejecutar con 4 hilos
OMP_NUM_THREADS=4 ./wator

# Analizar resultados
python wator_plot.py wator_poblacion.csv
```

## 📊 Comportamiento esperado

En una simulación típica:
- Las poblaciones oscilan en ciclos depredador-presa clásicos
- Los peces aumentan → los tiburones tienen comida → los tiburones aumentan
- Más tiburones → menos peces → tiburones se mueren de hambre → ciclo se repite

## 🎯 Notas de rendimiento

- El paralelismo es más efectivo con grillas grandes (N, M ≥ 100)
- OpenMP overhead es significativo para grillas pequeñas
- Ajusta `OMP_NUM_THREADS` según cores disponibles
- La visualización (`visualizar = true`) puede ralentizar la ejecución

## 📝 Licencia

Proyecto académico de Computación Paralela
