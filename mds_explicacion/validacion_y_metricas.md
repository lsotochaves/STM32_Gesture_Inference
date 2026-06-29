# Validación del sistema y métricas de performance

Esta guía explica cómo evaluar correctamente un clasificador de gestos y qué significan cada una de las métricas que produce `train.py`.

---

## Por qué evaluar correctamente importa

Un modelo que memoriza los datos de entrenamiento puede tener 100% de accuracy en train y 60% en el mundo real. La evaluación correcta mide qué tan bien **generaliza** el modelo a datos nuevos que nunca vio.

---

## División train / validación

El dataset completo se divide antes de entrenar:

```
Dataset completo (189 muestras)
    ├── Train (≈75%): el modelo aprende con estos datos
    └── Validación (≈25%): se usan solo para medir, nunca para entrenar
```

En este proyecto la división la hace `split.py`, que genera `train.csv` y `val.csv`.

**Regla fundamental:** nunca tomar decisiones de diseño (elegir features, ajustar hiperparámetros) mirando los resultados en validación repetidamente — eso contamina el conjunto de validación y hace que las métricas finales sean optimistas.

---

## Métricas por clase

Para cada clase el modelo reporta tres métricas calculadas a partir de la matriz de confusión.

### Matriz de confusión

Una tabla donde las filas son las clases reales y las columnas las clases predichas por el modelo.

```
                   → Predicho
                   h_shake   rest   v_shake
Real → h_shake:      16        0       3      ← 3 errores
       rest:          0       19       0      ← 0 errores
       v_shake:       3        0      16      ← 3 errores
```

Cada celda fuera de la diagonal es un error. Los valores en la diagonal son aciertos.

De cada fila/columna se extraen cuatro conteos:

| Símbolo | Nombre | Significado |
|---|---|---|
| TP | True Positive | Predijo esta clase y era correcto |
| FP | False Positive | Predijo esta clase pero era otra |
| FN | False Negative | Era esta clase pero predijo otra |
| TN | True Negative | No era esta clase y no la predijo |

---

### Precision

**¿De todo lo que el modelo dijo que era esta clase, cuánto realmente lo era?**

```
Precision = TP / (TP + FP)
```

Precision baja significa muchos **falsos positivos** — el modelo confunde otras clases con esta.

*Ejemplo:* si el modelo predice `horizontal_shake` 19 veces pero 3 eran en realidad `vertical_shake`, la precision es 16/19 = 0.842.

---

### Recall (Sensibilidad)

**¿De todos los ejemplos reales de esta clase, cuántos detectó el modelo?**

```
Recall = TP / (TP + FN)
```

Recall bajo significa muchos **falsos negativos** — el modelo no detecta todos los gestos de esta clase.

*Ejemplo:* había 19 ejemplos reales de `horizontal_shake` y el modelo identificó 16, entonces Recall = 16/19 = 0.842.

---

### F1-Score

**Media armónica entre Precision y Recall.** Resume ambas métricas en un solo número. Es más útil que el promedio simple cuando hay desbalance entre clases.

```
F1 = 2 × (Precision × Recall) / (Precision + Recall)
```

Un F1 de 1.0 es perfecto. Un F1 de 0.0 es el peor caso posible.

---

### Accuracy global

**Porcentaje total de predicciones correctas sobre todas las clases.**

```
Accuracy = total de aciertos / total de muestras
```

Es la métrica más intuitiva, pero puede ser engañosa si las clases están desbalanceadas. Si una clase tiene el 90% de los datos, un modelo que siempre prediga esa clase tiene 90% de accuracy sin aprender nada.

Con clases balanceadas (mismo número de muestras por clase, como en este proyecto), accuracy es una métrica confiable.

---

## Detección de sobreajuste (overfitting)

El sobreajuste ocurre cuando el modelo memoriza el conjunto de entrenamiento en lugar de aprender patrones generalizables.

### Cómo medirlo

Comparar accuracy en train vs. accuracy en validación:

```
Gap = Accuracy_train − Accuracy_val
```

| Gap | Interpretación |
|---|---|
| < 5% | Modelo bien generalizado |
| 5–10% | Sobreajuste leve — aceptable con pocos datos |
| 10–20% | Sobreajuste moderado — considerar regularización o más datos |
| > 20% | Sobreajuste severo — el modelo es inútil en producción |

En este proyecto el gap actual es **8.3%** (train 97.7%, val 89.5%) — leve, esperable con 189 muestras.

### Cómo reducir el sobreajuste

1. **Más datos**: es la solución más efectiva. Con el doble de muestras el gap típicamente se reduce a la mitad.
2. **Reducir complejidad del modelo**: para Decision Tree, bajar `max_depth`. Depth=3 da un gap de 5.7% con similar accuracy de validación.
3. **Cross-validation**: en lugar de una sola división train/val, usar K-fold para estimar el performance real con mayor confianza.

---

## Cross-validation K-fold

Con pocos datos, una sola división train/val puede dar resultados muy variables según qué muestras caen en cada parte. La cross-validation repite el proceso K veces:

```
División 1: [VAL] [TRN] [TRN] [TRN] [TRN]
División 2: [TRN] [VAL] [TRN] [TRN] [TRN]
División 3: [TRN] [TRN] [VAL] [TRN] [TRN]
División 4: [TRN] [TRN] [TRN] [VAL] [TRN]
División 5: [TRN] [TRN] [TRN] [TRN] [VAL]
```

El resultado final es el promedio y desviación estándar de las 5 evaluaciones:

```
CV mean ± std = 93.7% ± 3.9%
```

La **desviación estándar** indica qué tan sensible es el modelo a cómo se dividen los datos. Std alta (> 5%) indica que el dataset es pequeño o que hay inconsistencias en los datos.

---

## Curva de aprendizaje

Muestra cómo evoluciona el accuracy de validación a medida que se agregan más datos de entrenamiento.

```
Muestras  Val acc
   30      89%
   60      90%
   90      90%
  120      91%
  151      94%
```

**Cómo leerla:**
- Si la curva todavía sube al final → más datos mejorarán el modelo.
- Si la curva se aplanó → más datos no ayudan; hay que mejorar las features o el modelo.
- Si train acc >> val acc en todo el rango → sobreajuste estructural.

En este proyecto la curva todavía sube, indicando que más grabaciones mejorarán el performance.

---

## Parámetros que reporta `train.py`

Al ejecutar `train.py` se imprimen automáticamente:

| Output | Qué mide |
|---|---|
| `Train: N samples / Val: M samples` | Tamaño de cada subconjunto |
| `classification_report` | Precision, Recall, F1 por clase + accuracy global |
| `Confusion matrix` | Errores detallados por par de clases |
| `Tree structure` | Qué features y umbrales usa el árbol |

Para obtener adicionalmente las métricas de overfitting y cross-validation, ejecutar el análisis extendido de la sesión de exploración.

---

## Checklist de validación antes de desplegar en STM32

- [ ] Accuracy de validación >= 90%
- [ ] Gap train/val < 10%
- [ ] Ninguna clase tiene F1 < 0.80
- [ ] La matriz de confusión no muestra una clase siendo confundida sistemáticamente con otra
- [ ] Se probó con al menos 10 gestos reales en el dispositivo físico (no solo en Python)
- [ ] El pipeline de features en C produce los mismos valores que el script Python para las mismas entradas
