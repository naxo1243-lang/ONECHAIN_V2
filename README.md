# OneChain v3 (optimizado para grabar)

Un solo VST3 con cuatro secciones en serie: **Pitch -> Opto -> EQ lineal -> Master**, con interfaz alien (626x1024, redimensionable).

| Sección | Qué hace |
|---|---|
| Pitch | Motor original v1 (YIN + cuantizador de escala + desplazador de doble cabezal). Sin cambios. |
| Opto | Compresor óptico (estilo LA-2A): release dependiente del programa, saturación suave, **Link** estéreo y **Mix** paralelo. |
| EQ lineal | Ecualizador parametrico de **fase lineal** de 8 bandas (estilo Pro-Q): Bell, Low/High Shelf, Low/High Cut (6-96 dB/oct), Notch, Band Pass, Tilt. Cada banda en Stereo/Left/Right/Mid/Side. Oculto tras el boton **EQ** de la cabecera. |
| Master | EQ (low cut, shelves, campana) -> **multibanda de 3 bandas** (comp + sat + nivel por banda) -> glue comp con mix -> drive con mix -> **auto-gain** -> ancho + **bass mono** -> **limitador true-peak**. |

## Interfaz
- Página principal: los controles de la imagen (Pitch On, Key, Scale, Retune Speed, Pitch Amount, Opto, Master...).
- Botón del alien (abajo a la derecha): alterna a **ADV**, con todos los controles avanzados del Master.
- Abajo a la izquierda: selector de presets. Medidor de salida en el centro.

## EQ lineal (boton EQ, arriba a la derecha)
- Arrastra un nodo: frecuencia y ganancia. Rueda del raton sobre un nodo: Q.
- Doble clic en vacio: agrega una banda. Doble clic en un nodo: la quita.
- Fila de 8 fichas para elegir banda; debajo: Shape, Slope (solo cortes), Placement y los knobs Freq / Gain / Q.
- Analizador de espectro (post-EQ) con boton Spectrum; el boton de dB cambia el rango de la grafica.

## Modos de latencia (selector arriba, junto al boton EQ)
| Modo | Pitch (ventana) | EQ | Limitador | Latencia total @48k |
|---|---|---|---|---|
| **Recording** (por defecto) | 10 ms | cero latencia (IIR) | 0.5 ms | **5.7 ms** |
| Original (como la v1) | 25 ms | cero latencia (IIR) | 1.5 ms | 14.2 ms |
| Mixing | 25 ms | fase lineal | 1.5 ms | 62.2 ms |

La v1 tenia 14 ms. El selector informa el cambio de latencia al host. Cargar un preset no cambia el modo.
El EQ de cero latencia y el de fase lineal tienen la misma magnitud (diferencia < 0.01 dB).

## CPU (cadena completa con los valores por defecto, un nucleo, referencia en un PC de pruebas)
| | v2.1 | v3 Recording | v3 Original | v3 Mixing |
|---|---|---|---|---|
| CPU | ~12 % | **~1.7 %** | ~1.7 % | ~3.7 % |

Que se optimizo (sin quitar funciones ni cambiar el sonido):
- Detector de tono YIN sobre senal decimada (~12 kHz) + afinado a frecuencia completa: 10.7x menos CPU, misma precision.
- Desplazador de pitch estereo con calculos compartidos; si Pitch esta apagado no analiza.
- log / exp / tanh rapidos en los bucles por muestra; coeficiente de release sin exp().
- Limitador con minimo movil O(1); interpola picos solo cuando la senal esta cerca del techo.
- Crossover multibanda con un solo all-pass; etapas neutras se saltan; medidores una vez por bloque.
- EQ de cero latencia: solo las bandas activas cuestan CPU.

## Probar el DSP sin JUCE
    g++ -std=c++17 -O2 Tests/dsp_test.cpp -o dsp_test && ./dsp_test

## Compilar
GitHub Actions (carpeta .github) o en Windows con CMake + Visual Studio:
    cmake -B build -A x64
    cmake --build build --config Release --target OneChain_VST3

## Experimental
`Source/Dsp/Experimental/PsolaPitch.h`: motor de pitch PSOLA (precision ~0.1 centesimos, formantes
preservados). No se usa; queda guardado como modo alternativo. Su prueba esta en `Experimental/`.
