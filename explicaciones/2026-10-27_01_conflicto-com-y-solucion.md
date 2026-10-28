# Por qué COM6 fallaba y qué cambió al usar COM10

Fecha: 27/10/2026. Cambio aplicado con autorización explícita del usuario. Evidencias locales: E-024 y E-025.

## Qué cambió

Se cambió **la asignación de Windows de un adaptador USB-serie CH343: de COM6 a COM10**. No se cambió la arquitectura del teclado, el firmware, sus particiones ni el pinout durante esta reparación.

| Antes | Después |
|---|---|
| El CH343 figuraba como COM6, con Error 31 | El mismo adaptador figura como COM10, estado OK, problema 0 |
| El nombre activo COM6 dirigía a un enlace Bluetooth | COM10 dirige al dispositivo serie del CH343 |
| `flash-id` terminaba con Write timeout | `flash-id` identifica ESP32-S3 y memoria a 115200 y 460800 baudios |

## Las piezas del recorrido

```text
esptool en la PC
    ↓ abre un nombre de Windows, por ejemplo COM10
adaptador USB-serie CH343
    ↓ comunica por UART
cargador ROM del ESP32-S3
    ↓ permite consultar o escribir la flash
memoria flash
```

Un COM es un nombre del enlace de comunicación. No es un GPIO, una partición ni una versión de firmware. Windows puede asignar uno diferente a cada adaptador.

El USB nativo del ESP cumple otro papel en nuestra prueba: después de cargar firmware, permite que Windows lo reconozca como HID y que el configurador se comunique con él. El configurador no reemplaza a esptool en el procedimiento utilizado.

## Qué vimos para diagnosticarlo

1. `serial.tools.list_ports -v` mostraba dos entradas COM6: Bluetooth y CH343.
2. `Get-PnpDevice` mostraba el CH343 en Error. `Get-PnpDeviceProperty` devolvía problema **31** y estado interno **0xC0000035**.
3. `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` incluía COM6 para Bluetooth, sin publicar el CH343 como un puerto activo.
4. `QueryDosDeviceW("COM6")` confirmó que abrir COM6 dirigía a `\Device\BthModem3`.
5. `flash-id` reprodujo el timeout antes de identificar el chip; no había comenzado una escritura.

La enumeración PnP decía qué dispositivo intentaba usar el nombre. La consulta del alias activo decía a qué dispositivo llegaba realmente ese nombre. Esa diferencia permitió localizar el fallo en la configuración de Windows.

El código 31 por sí solo no identifica una colisión: el diagnóstico se apoyó también en el estado interno, las dos asignaciones y el alias efectivo. Tampoco es correcto concluir «placa dañada» a partir de un Write timeout.

## Cómo se hizo el cambio

Después de recibir «autorizo el cambio»:

1. Se comprobó la instancia exacta del CH343 y su PortName anterior. Se guardó una instantánea local de PortName y FriendlyName.
2. Se consultó la reserva de números COM. COM8 y COM9 estaban reservados; COM10 estaba libre.
3. Se ejecutó una operación puntual con privilegios de administrador de Windows, mediante UAC.
4. Se reservó COM10 con `ComDBOpen` y `ComDBClaimPort`, sin forzar una reserva ocupada.
5. Se cambió `PortName` a COM10 en la clave `Device Parameters` de ese adaptador y se ajustó su `FriendlyName` visible.
6. Se solicitó `pnputil /restart-device` únicamente para esa instancia CH343.
7. **No se liberó COM6**, porque seguía perteneciendo a Bluetooth.

La reserva en ComDB evita tratar un número ocupado como libre; cambiar solo el texto del nombre visible no habría reparado el enlace. El script puntual quedó en `tools/reassign_ch343_com10.py`, ligado a la instancia local autorizada. No debe reutilizarse para otras placas ni ejecutarse como paso normal del flasheo.

## Cómo se comprobó

- CH343: estado OK, código de problema 0.
- `QueryDosDeviceW("COM10")`: dispositivo serie activo; COM6 conservó su destino Bluetooth.
- `esptool --chip esp32s3 --port COM10 --baud 115200 flash-id`: exit 0.
- Misma consulta a 460800 baudios, velocidad del flasheador: exit 0.
- El chip reportó ESP32-S3 revisión v0.2, flash de 16 MB y PSRAM incorporada de 8 MB.

`flash-id` consulta la memoria: no escribe firmware ni borra flash. Esptool cargó su stub temporal en RAM y reinició la placa al terminar. Los registros y el estado antes/después están bajo `evidence/T-024/2026-10-27/` en el espacio local.

## Qué se mejoró en el flasheador

Antes se filtraban entradas CH343 de `list_ports`. Ese filtro podía encontrar un adaptador fallido cuyo nombre COM estaba ocupado por Bluetooth.

Ahora `query_port_target()` consulta `QueryDosDeviceW`; `reject_bluetooth_target()` rechaza destinos BthModem antes de abrir puerto. La consulta se repite antes de escribir y se comprueba que el adaptador no haya cambiado. No reasigna puertos automáticamente.

La protección detecta esta colisión concreta y cambios de destino. No certifica por sí sola todos los problemas posibles de drivers ni la identidad del ESP: después sigue siendo necesaria la consulta del chip. Ocho pruebas locales del flasheador aprobaron, incluida la detección de Bluetooth.

## Pendiente y recuperación

Esta reparación confirmó el enlace de **esa** placa, no su firmware ni su lectura desde el configurador. Cada adaptador nuevo puede tener otro COM y necesita su identificación.

Se conservaron los valores anteriores para recuperación. No conviene restaurar COM6 mientras siga ocupado; hacerlo recrearía el conflicto. Una reversión debe limitarse a este adaptador y a la reserva COM10, sin liberar reservas ajenas. No se ensayó una reversión porque el cambio quedó verificado.

## Fuentes técnicas

- [Microsoft: códigos de error del Administrador de dispositivos](https://support.microsoft.com/es-es/windows/hardware/drivers/error-codes-in-device-manager-in-windows).
- [Microsoft: QueryDosDeviceW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-querydosdevicew).
- [Microsoft: ComDBClaimPort](https://learn.microsoft.com/en-us/windows/win32/api/msports/nf-msports-comdbclaimport).
- [Espressif: diagnóstico de excepciones serie](https://docs.espressif.com/projects/esptool/en/latest/esp32/troubleshooting.html#a-serial-exception-error-occurred).
