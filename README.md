# OneChain v2

Un solo VST3 con tres secciones en serie: **Pitch -> Opto -> Master**, con interfaz alien (626x1024, redimensionable).

| Sección | Qué hace |
|---|---|
| Pitch | Motor original v1 (YIN + cuantizador de escala + desplazador de doble cabezal). Sin cambios. |
| Opto | Compresor óptico (estilo LA-2A): release dependiente del programa, saturación suave, **Link** estéreo y **Mix** paralelo. |
| Master | EQ (low cut, shelves, campana) -> **multibanda de 3 bandas** (comp + sat + nivel por banda) -> glue comp con mix -> drive con mix -> **auto-gain** -> ancho + **bass mono** -> **limitador true-peak**. |

## Interfaz
- Página principal: los controles de la imagen (Pitch On, Key, Scale, Retune Speed, Pitch Amount, Opto, Master...).
- Botón del alien (abajo a la derecha): alterna a **ADV**, con todos los controles avanzados del Master.
- Abajo a la izquierda: selector de presets. Medidor de salida en el centro.

## Latencia (constante, con las secciones encendidas o apagadas)
Pitch 12.5 ms + limitador 1.7 ms. El plugin la informa al host.

## Probar el DSP sin JUCE
    g++ -std=c++17 -O2 Tests/dsp_test.cpp -o dsp_test && ./dsp_test

## Compilar
GitHub Actions (carpeta .github) o en Windows con CMake + Visual Studio:
    cmake -B build -A x64
    cmake --build build --config Release --target OneChain_VST3

## Experimental
`Source/Dsp/Experimental/PsolaPitch.h`: motor de pitch PSOLA (precision ~0.1 centesimos, formantes
preservados). No se usa; queda guardado como modo alternativo. Su prueba esta en `Experimental/`.
