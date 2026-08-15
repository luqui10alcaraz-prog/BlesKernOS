# BlesKernOS 0.8 — parche de compatibilidad Win32, fase 1

## Objetivo

Este parche amplía la base necesaria para ejecutar aplicaciones Win32 de la era
Windows 95/98. No pretende convertir BlesKernOS en una implementación completa de
Windows; prioriza las rutas de ejecución que más programas usan: memoria virtual,
procesos, archivos, file mappings, DIB/GDI, colas de mensajes, controles comunes y
registro.

La semántica se contrastó con la documentación pública de Win32 y con la
organización/los casos límite visibles en el código fuente de Wine. El parche **no
copia código textual de Wine**. Si en el futuro se incorpora o adapta código de
Wine, ese componente debe conservar desde el primer commit los avisos y requisitos
de la LGPL aplicables.

## Cambios incluidos

### Memoria virtual lógica por proceso

- Tabla de regiones asociada a `process_id`.
- `VirtualAlloc`: `MEM_RESERVE`, `MEM_COMMIT` y combinación de ambos.
- `VirtualFree`: `MEM_DECOMMIT` y `MEM_RELEASE`.
- `VirtualProtect` y `VirtualQuery` con estado/protección coherentes.
- Limpieza de regiones al finalizar el proceso.
- `VirtualQueryEx` y `VirtualProtectEx` validan el handle; el acceso cruzado se
  rechaza mientras no existan espacios de direcciones separados.

### Procesos y handles

- `CreateProcessA` devuelve `hProcess`, `hThread`, `dwProcessId` y `dwThreadId`.
- `OpenProcess`, `TerminateProcess`, `GetExitCodeProcess` y `GetProcessId`.
- Espera de handles de proceso mediante `WaitForSingleObject` y
  `WaitForMultipleObjects`.
- Handles ligados al proceso propietario y limpieza automática.

### Archivos y file mappings

- Los handles Win32 ya no mantienen una copia privada completa del archivo.
- Lectura, escritura, seek, tamaño, truncado y flush sobre descriptores VFS.
- Modos de creación Win32 y comprobación básica de `FILE_SHARE_READ/WRITE`.
- `CreateFileMappingA/W`, `MapViewOfFile`, `FlushViewOfFile` y
  `UnmapViewOfFile`.
- Mappings anónimos y respaldados por archivo.

### DIB y GDI

- `CreateDIBSection` y `CreateDIBitmap` para `BI_RGB` de 16, 24 y 32 bpp.
- `GetDIBits`, `SetDIBits`, `SetDIBitsToDevice` y `StretchDIBits`.
- Sincronización bidireccional entre el puntero de bits expuesto a la aplicación y
  el bitmap seleccionado en un DC de memoria.
- `BitBlt`: `SRCCOPY`, `SRCAND`, `SRCPAINT`, `SRCINVERT` y `NOTSRCCOPY`.
- `SaveDC`/`RestoreDC` para DC de memoria.

### USER32 y controles comunes

- Filtros reales de `hwnd` y rango de mensajes en `GetMessage`/`PeekMessage`.
- `WM_WINDOWPOSCHANGING` y `WM_WINDOWPOSCHANGED` en `SetWindowPos`.
- `SetPropA/W`, `GetPropA/W` y `RemovePropA/W`.
- `GetWindow`, `GetTopWindow`, `SetParent` y `GetAncestor`.
- Base funcional de `SysListView32`, `SysTreeView32` y `SysTabControl32`.
- Inserción, borrado, consulta y selección básicas, más `WM_NOTIFY` síncrono.

### Registro

- Más capacidad de claves y valores sin hacer crecer peligrosamente la `.bss`.
- Formato persistente versión 2, con lectura compatible del formato anterior.
- `RegEnumKeyA`, `RegEnumKeyExA/W`, `RegEnumValueA/W` y
  `RegQueryInfoKeyA`.
- `RegFlushKey` y variantes Unicode de las operaciones principales.
- Reconocimiento de `REG_EXPAND_SZ` y `REG_MULTI_SZ`.

## Límites arquitectónicos que siguen abiertos

### No hay aislamiento de páginas real

`paging.c` mantiene un mapeo identidad compartido de 4 GiB con páginas grandes,
marcadas accesibles y escribibles. Por eso:

- `VirtualProtect` registra la protección, pero la CPU todavía no la impone.
- Las reservas son regiones del heap compartido, no rangos virtuales privados.
- No se puede garantizar el mismo `ImageBase` para dos procesos simultáneos.
- `VirtualAlloc` con dirección fija solo puede confirmar/activar una reserva creada
  por el propio subsistema.

Resolverlo exige directorios de páginas por proceso, cambio de CR3, mapeos del
kernel compartidos y una estrategia explícita para las superficies/objetos GUI que
hoy contienen punteros del kernel.

### El PE loader todavía relocaliza desde el heap

Las imágenes siguen cargándose con `kzalloc`. Un PE sin tabla `.reloc` solo puede
funcionar si termina en su base preferida, algo que este parche no puede prometer
sin el rediseño anterior.

### Escritura FAT

Win32 ya hace I/O por descriptor, pero el backend FAT actual no dispone de una
primitiva completa de escritura parcial por clúster. Algunas escrituras terminan
reconstruyendo el archivo dentro de la capa FAT/VFS. Se eliminó la copia adicional
del handle Win32, no la limitación del filesystem.

### File mappings

Los mappings se materializan como buffers contiguos del heap. No son vistas de
páginas compartidas ni implementan copy-on-write real. `FlushViewOfFile` persiste
el buffer completo cuando corresponde.

### Controles comunes

ListView, TreeView y Tab cubren el camino básico, no toda COMCTL32:

- ListView: una columna de texto útil; subitems avanzados y custom draw pendientes.
- TreeView: estructura visual aplanada; jerarquía y navegación parcial.
- Tab: selección e items básicos; layout/scroll/owner draw incompletos.
- Las notificaciones son parciales.

### Registro

Sigue siendo una base estática y acotada, no un hive dinámico. No implementa
seguridad, notificaciones, carga/descarga de hives ni valores arbitrariamente
grandes.

## Compilación verificada

La validación mínima del parche es:

```sh
make -j2 build/kernel.elf
```

El kernel enlaza sin errores con `gcc -m32` y `ld -m elf_i386`. Los ejecutables de
prueba Win32 ya presentes no se recompilaron en el entorno de generación porque no
estaba instalado `i686-w64-mingw32-gcc`; se conservaron los binarios existentes.

## Pruebas recomendadas en QEMU

1. Ejecutar las pruebas Win32 ya incluidas: VM/TLS, hilos, sincronización,
   recursos, menús, diálogos, edición y WineCalc compatibility.
2. Añadir una prueba que reserve y confirme memoria por separado, consulte la
   región y cambie su protección.
3. Crear un archivo, mapearlo, modificar el buffer, hacer flush, desmontar y volver
   a leerlo.
4. Crear una DIB de 32 bpp, escribir directamente en `bits`, hacer `BitBlt` y luego
   dibujar con GDI para comprobar la sincronización inversa.
5. Crear ListView/TreeView/Tab y registrar cada `WM_NOTIFY` recibido por el padre.
6. Crear claves y valores, enumerarlos, reiniciar el sistema y repetir la consulta.

## Aplicación del parche

Desde la raíz del source original:

```sh
patch -p1 < BlesKernOS-0.8-win32-phase1.patch
make -j2 build/kernel.elf
```
