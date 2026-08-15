# Bibliotecas Win32 externas

Las imágenes de BlesKernOS no incluyen DLL extraídas de Windows ni de otra
instalación propietaria. La compatibilidad base (`KERNEL32`, `USER32`, `GDI32`,
`COMCTL32`, etc.) se implementa en el código fuente de `kernel/win32/`.

Wine es software libre, pero sus módulos se distribuyen bajo LGPL y no se
pueden copiar como reemplazo directo: dependen de su propio entorno de Wine y
de APIs que BlesKernOS no implementa. Si se incorpora código o una DLL de un
proyecto libre en el futuro, debe añadirse junto con su licencia, atribución y
un build reproducible desde fuentes.

`WZINET32.DLL` y `WZUN95.DLL` son complementos de WinZip, no bibliotecas del
sistema operativo. Deben provenir de una distribución de WinZip autorizada;
no hay un reemplazo compatible de Wine para ellas.
