# Cómo entrenar un clasificador de gestos

Esta guía explica el proceso completo de entrenamiento desde cero, qué decisiones tomar y por qué.

---

## Visión general del pipeline

```
Sensor (STM32) → CSV raw → Extracción de features → Entrenamiento → Modelo serializado → Inferencia en STM32
```

Cada etapa tiene decisiones de diseño propias. A continuación se explica cada una.

---

## 1. Recolección de datos

Antes de entrenar cualquier modelo necesitás datos etiquetados: grabaciones de cada gesto con su nombre correspondiente.

### Qué necesitás decidir

- **Clases**: cuántos gestos distintos va a reconocer el sistema. Cada clase necesita suficientes ejemplos para que el modelo aprenda a generalizarla, no a memorizar.
- **Cantidad mínima por clase**: con menos de 30 ejemplos el modelo es muy inestable. Con 50–100 ejemplos por clase se puede trabajar bien. Más de 150 es ideal.
- **Consistencia**: todos los datos deben grabarse bajo las mismas condiciones de orientación, velocidad y duración. Las inconsistencias entre sesiones de grabación son la causa más común de errores.
- **Clase "unknown"**: si el sistema va a funcionar en un ambiente real, necesitás grabar también movimientos no intencionales (acomodar el board, temblor, golpe accidental) como una clase extra. Sin ella, el modelo siempre clasifica algo aunque la entrada no sea un gesto válido.

### Calibración

El STM32 debe medir un período de reposo antes de cada gesto para estimar el bias (offset DC) de cada eje y restarlo. Si no se hace, los datos de entrenamiento y los de inferencia tendrán distribuciones distintas y el modelo fallará en el dispositivo aunque funcione bien en la computadora.

---

## 2. Extracción de features

Un clasificador clásico no trabaja sobre la señal cruda en el tiempo — trabaja sobre un **vector de características** (features) que resume cada grabación en un conjunto de números.

### Por qué no usar la señal cruda

- Las grabaciones tienen longitudes distintas (el gesto puede durar más o menos).
- La señal cruda tiene cientos de puntos; el vector de features tiene decenas.
- En el STM32, el vector de features es lo que cabe cómodamente en RAM para inferencia.

### Qué features calcular

Para señales de giroscopio, las features más útiles por eje son:

| Feature | Qué captura |
|---|---|
| Media | Dirección dominante del movimiento |
| Desviación estándar | Variabilidad / irregularidad |
| Mínimo y máximo | Amplitud pico a pico |
| Rango (max − min) | Magnitud total del movimiento |
| Energía (media de cuadrados) | Intensidad normalizada por duración |
| Zero Crossing Rate (ZCR) | Frecuencia aproximada del gesto |

Estas se calculan para cada eje (Gx, Gy, Gz) y para la magnitud vectorial sqrt(Gx²+Gy²+Gz²), dando un vector de ~25 números por grabación.

### Normalización de orientación

Si el board puede estar en distintas orientaciones cardinales durante la grabación, los datos deben transformarse a un marco de referencia fijo antes de extraer features. De lo contrario, el mismo gesto físico produce vectores de features completamente distintos según cómo esté orientado el board.

La transformación para orientación en el plano horizontal (board siempre plano, eje Z invariante):

| Dirección | Gx_ref | Gy_ref | Gz_ref |
|---|---|---|---|
| Norte (referencia) | Gx | Gy | Gz |
| Este | Gy | −Gx | Gz |
| Sur | −Gx | −Gy | Gz |
| Oeste | −Gy | Gx | Gz |

---

## 3. Elección del clasificador

Un clasificador es una función matemática que recibe el vector de features y devuelve una etiqueta de clase.

### Decision Tree (Árbol de decisión)

Un árbol de decisión divide el espacio de features con preguntas del tipo `feature_X > umbral`. Cada hoja del árbol es una clase.

**Ventajas para STM32:**
- Se puede serializar directamente como código C (`if-else` anidados).
- No requiere multiplicaciones de punto flotante complejas.
- Interpretable: podés leer el árbol y entender por qué clasificó algo.

**Desventajas:**
- Tiende a sobreajustar (memorizar) si se le permite crecer demasiado.
- Sensible al desbalance de datos entre clases.

**Hiperparámetro clave:** `max_depth` — la profundidad máxima del árbol. A más profundidad, más capacidad pero más riesgo de sobreajuste. Para este proyecto, depth 3–4 es suficiente.

### Random Forest

Un conjunto de muchos árboles de decisión entrenados con subconjuntos aleatorios de los datos. La clasificación final es la votación mayoritaria.

**Ventajas:**
- Más robusto que un solo árbol.
- Da probabilidades por clase (útil para detectar gestos dudosos).

**Desventajas:**
- Más difícil de serializar para STM32 — requiere código C más grande.
- Más lento en inferencia.

### SVM (Support Vector Machine)

Encuentra el hiperplano que mejor separa las clases en el espacio de features.

**Ventajas:**
- Muy efectivo con pocos datos y features bien elegidas.
- El kernel lineal se serializa fácilmente como un producto punto.

**Desventajas:**
- No da probabilidades directamente.
- El kernel RBF (no lineal) es difícil de llevar a C embebido.

---

## 4. División de datos: train / validación

Nunca evaluás el modelo con los mismos datos con los que lo entrenaste — eso daría resultados artificialmente perfectos. El procedimiento estándar es:

1. **Dividir el dataset** en dos subconjuntos: train (70–80%) y validación (20–30%).
2. **Entrenar** solo con el subconjunto train.
3. **Evaluar** con el subconjunto de validación — datos que el modelo nunca vio.

La diferencia entre accuracy en train y en validación indica el grado de sobreajuste.

### Cross-validation (K-fold)

Con pocos datos (< 300 muestras), la división aleatoria puede dar resultados variables según qué muestras caen en cada subconjunto. La cross-validation divide los datos en K partes, entrena K veces usando K−1 partes y evalúa en la restante, promediando los resultados. Esto da una estimación más confiable del performance real.

---

## 5. Serialización a C para STM32

Una vez entrenado el modelo, necesitás convertirlo a código que el STM32 pueda ejecutar sin sistema operativo ni librerías de Python.

### Para Decision Tree / Random Forest

La librería `m2cgen` convierte el modelo entrenado de scikit-learn directamente a una función C:

```python
import m2cgen as m2c
c_code = m2c.export_to_c(clf)
```

El resultado es una función `score(double *input, double *output)` que calcula los scores por clase. Se incluye en el proyecto STM32 como un archivo `.h`.

### Para SVM lineal

Un SVM lineal es equivalente a `output = W · x + b`, donde W es la matriz de pesos y b el bias. Ambos se pueden exportar como arrays C estáticos.

### Lo que el STM32 debe hacer en inferencia

1. Mantener quieto un período corto → estimar bias por eje.
2. Detectar onset del gesto (energía supera umbral).
3. Grabar exactamente N muestras.
4. Calcular el mismo vector de 25 features que se usó en entrenamiento.
5. Pasar el vector a la función `predict()` generada.
6. Leer el índice de la clase ganadora y actuar en consecuencia.

**Crítico:** los pasos 1 y 4 deben ser idénticos en código al pipeline de entrenamiento. Cualquier diferencia en cómo se calcula el bias o las features genera un desfase entre las distribuciones de entrenamiento e inferencia, y el modelo falla aunque tenga 99% de accuracy en validación.

---

## 6. Flujo de desarrollo resumido

```
1. Definir clases y recolectar datos con receiver.py
2. Ejecutar feature_extractor.py → training_features.csv
3. Ejecutar split.py → train.csv + val.csv
4. Ejecutar train.py → evaluar métricas + generar classifier.h
5. Si el performance no es suficiente:
   a. Recolectar más datos
   b. Agregar/modificar features
   c. Cambiar clasificador o ajustar hiperparámetros
6. Incluir classifier.h en el proyecto STM32
7. Implementar el mismo pipeline de features en C
8. Validar en el dispositivo real
```
