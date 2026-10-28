# Cómo está organizado el orquestador

Fecha: 27/10/2026. Esta explicación describe la organización local creada hasta ahora. Al documentarse, el espacio local TecleadosOrch todavía no tenía Git. Estas explicaciones se preparan para el fork TecleadosAr/Tecleados-ESP-Firmware-Theo-Local; su presencia allí no replica automáticamente todas las carpetas locales.

## Qué responsabilidad tiene cada parte

```text
TecleadosOrch/
  AGENTS.md, ROADMAP.md, TASKS.md, VALIDATION.md, DECISIONS.md, STATUS.md
  explicaciones/       aprendizaje y desglose de cambios
  tools/              consultas de seguimiento y utilidades técnicas
  flasher/            firmware congelado, lanzador Python y logs por intento
  Flash_MK1.cmd        entrada manual de Windows al flasheador
  sources/            copia de referencia del firmware
  work/               copia de trabajo y compilación
  evidence/           pruebas, hashes, capturas y respaldos locales
  outputs/            entregables y archivos auxiliares locales
```

La estructura distingue el estado del proyecto, las herramientas que operan y las pruebas que justifican las conclusiones. No agrega servidores, agentes permanentes, servicios de Windows ni una base de datos paralela.

## El motor de seguimiento

`tools/orchestrator.py` lee TASKS.md. Los comandos `status`, `plan`, `check` y `review` muestran información o comprueban coherencia; no flashean, compran, reasignan puertos ni cambian estados automáticamente.

`check` revisa campos, estados permitidos, identificadores, dependencias, referencias de evidencia para DONE y hashes del material recibido. Puede detectar una evidencia ausente, pero no determinar por sí mismo que una tecla física funcionó.

## El flasheador

```text
Flash_MK1.cmd
  → Python instalado de ESP-IDF
    → flash_mk1.py
      → verifica los archivos contra release-manifest.json
      → lista CH343 USB y consulta el destino efectivo del COM
      → esptool identifica ESP32-S3 y memoria
      → ofrece respaldo y pide la elección de escritura
      → escribe los archivos de flash_args y verifica hashes
      → conserva el log y el resultado del intento
```

El acceso `.cmd` es un lanzador de texto, no un ejecutable compilado ni una instalación. Esptool es la herramienta que escribe realmente la flash. El Python y esptool ya estaban disponibles en la PC.

La imagen congelada evita cargar por accidente el último build experimental de `work/`. El manifest permite verificar que sus archivos no cambiaron. Una carga verificada no equivale a probar arranque, configurador y teclas: son pasos de aceptación distintos.

## Qué implica una operación de Windows

La reasignación de COM fue una intervención puntual autorizada, no una acción automática del flasheador. Necesitó UAC y cambió solo la instancia aprobada del CH343. Su script contiene datos locales y no se debe publicar o generalizar sin adaptar su alcance.

## Cómo se conservarán las explicaciones

Cada cambio significativo tendrá una nota en `explicaciones/`, enlazada desde su índice, que responda:

1. Qué problema observable había.
2. Qué se cambió y por qué.
3. Cómo funciona el mecanismo.
4. Qué se ejecutó, con qué autorización y alcance.
5. Cómo se verificó y qué sigue pendiente.
6. Cómo recuperarse o revertir, si corresponde.

Las explicaciones complementan la evidencia y las decisiones, sin copiar todos los logs. Se prioriza explicar para que el usuario pueda repetir el procedimiento. Un pedido de explicación no autoriza ejecutar operaciones físicas.

## Alcance de publicación

Para un PR se seleccionan código, pruebas y documentación necesarios. Respaldos completos de flash, logs crudos con identificadores, fotos personales, inventarios de backup, entornos de dependencias y directorios de build permanecen locales. La carpeta de evidencias no se publica íntegra por defecto.

El PR del orquestador debe declarar si el código se agregó por primera vez o modificó una implementación existente. Eso depende de su repositorio de destino; no puede determinarse solo a partir de esta carpeta sin Git.

## Alcance de este PR de documentación

El PR agrega estas explicaciones y la convención de mantenerlas. No agrega la herramienta de flasheo, no distribuye binarios, no modifica C y no reasigna puertos de quien lea el repositorio. Los nombres de herramientas locales permiten identificar el mecanismo que se usó, sin afirmar que esos archivos estén incluidos en este PR.
