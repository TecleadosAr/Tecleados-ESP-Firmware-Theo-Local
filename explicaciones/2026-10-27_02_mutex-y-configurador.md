# Por qué el configurador se quedaba esperando

Fecha: 27/10/2026. Base: firmware `4c8cca1f1810c54dad851136e351fe3ce3e9f670` y corrección local `tx-recursive-mutex-1`. Evidencias E-018 a E-022.

## Qué cambió

En `components/comm_module/comm_tx.c`, el mutex de transmisión pasó de ordinario a recursivo. Todas sus tomas y liberaciones se cambiaron a las APIs recursivas correspondientes. Se conservó el formato del protocolo y el pinout.

Un mutex protege un recurso para que dos tareas no lo modifiquen a la vez. Un mutex ordinario no permite que su propia tarea lo tome de nuevo sin haberlo liberado. Un mutex recursivo sí permite esa entrada anidada; cada toma debe tener su liberación correspondiente.

## El bloqueo

```text
comm_tx_task toma el mutex TX
  → envía el primer paquete
  → pide reprogramar el temporizador de timeout
     → se consulta el instante de último envío
        → el getter intenta tomar el mismo mutex TX
           → espera indefinidamente; la tarea aún lo tiene tomado
```

La web podía abrir la interfaz y recibir un primer paquete. Eso explicaba por qué se veía LIVE y «2 packets expected», pero no se completaba la respuesta. Abrir WebHID y completar una transacción son pruebas diferentes.

## La corrección y su alternativa

Se usó `xSemaphoreCreateRecursiveMutex` junto a `xSemaphoreTakeRecursive` y `xSemaphoreGiveRecursive` en todos los accesos al mutex TX. La configuración de FreeRTOS de IDF 6.0.2 ya habilita mutex recursivos.

Otra solución sería reorganizar todas las llamadas al temporizador para ejecutarlas fuera de las secciones protegidas. Esa alternativa toca más rutas; el candidato eligió conservar el diseño actual y permitir la anidación existente. No es una regla general para convertir todos los mutex en recursivos.

## Qué se probó

1. Se preservaron los binarios previos y la copia de referencia del repositorio.
2. Se compiló el candidato con IDF 6.0.2; el binario entró en su partición de 2 MB.
3. Se cargó en la placa identificada, verificando las tres regiones escritas por hash.
4. El log de arranque coincidió con el ELF del candidato, pasó el test de PSRAM e inicializó USB.
5. Tras pasar al USB nativo, el log aportado por el usuario registró **54 respuestas correctas**, incluidas **44 de estado**, **3 transferencias multipartes completas** y **cero timeouts ni avisos de CRC**.

El incidente de lectura del configurador quedó resuelto en esa sesión. No se realizaron ensayos de teclas físicas, persistencia de cambios, BLE ni estabilidad prolongada como parte de esa aceptación. Los mensajes previos de ahorro de energía y servicio ISR GPIO se conservaron como asuntos separados.

## Dónde está y qué falta publicar

La corrección está en la copia de trabajo `work/firmware-4c8cca1/`. La copia `sources/Tecleados-ESP-Firmware/` se conservó intacta. Patch, manifest y resultados están en `evidence/T-021/2026-10-27/` localmente.

Guardar esta explicación en un repositorio del orquestador no aplica automáticamente el parche al repositorio del firmware. Tampoco sustituye una revisión de código allí. Esa publicación debe tener su propio alcance explícito.
