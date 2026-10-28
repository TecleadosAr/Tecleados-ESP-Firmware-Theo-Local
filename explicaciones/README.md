# Explicaciones de Tecleados MK1

Este archivo es el punto de entrada para entender los cambios. Las explicaciones se guardan por fecha y tema; un cambio posterior agrega una actualización, sin reescribir lo que se sabía antes.

| Explicación | Qué vas a aprender | Estado |
|---|---|---|
| [01 — Conflicto COM6 y reasignación a COM10](2026-10-27_01_conflicto-com-y-solucion.md) | Por qué fallaba la consulta, qué se cambió en Windows y cómo se verificó | Cambio aplicado y enlace verificado |
| [02 — Bloqueo del firmware y mutex recursivo](2026-10-27_02_mutex-y-configurador.md) | Por qué llegaba un paquete pero se detenía la respuesta del configurador | Corrección probada por lectura del configurador |
| [03 — Organización del orquestador y flasheador](2026-10-27_03_estructura-orquestador.md) | Qué responsabilidad tiene cada carpeta y qué acciones modifican hardware | Herramientas locales entregadas; alcance de cada prueba explicitado |

Para próximos cambios: [plantilla de explicación](PLANTILLA.md).

Estas notas explican decisiones y procedimientos. En el espacio local TecleadosOrch, TASKS.md conserva los estados de tareas, DECISIONS.md las decisiones y VALIDATION.md el catálogo de evidencias. Esos registros completos no se incorporan desde este PR de documentación. Los logs originales, respaldos e identificadores de dispositivos permanecen en el espacio local; no se adjuntan automáticamente a un PR.

## Cómo leerlas

Primero leer «Qué cambió», luego el desglose y la verificación. Un resultado verificado en una placa no demuestra que todas las demás placas estén programadas o aceptadas. Las secciones «Pendiente» señalan lo que todavía falta comprobar.
