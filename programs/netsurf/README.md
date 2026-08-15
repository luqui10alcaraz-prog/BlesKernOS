# NetSurf 3.11 para BlesKernOS — etapa 16: layout estructural

Esta etapa amplía el frontend construido con Hubbub, libwapcaplet y libcss para
que páginas HTML estáticas con formularios y CSS clásico se parezcan mucho más
a su estructura original. Continúa siendo un frontend ligero propio sobre las
bibliotecas de NetSurf; todavía no incorpora JavaScript ni todo el motor de
layout del navegador oficial.

## Clasificación del proyecto

Este árbol es un **port parcial con frontend propio**, no un fork completo de
NetSurf. Reutiliza y adapta Hubbub, libcss, libparserutils y libwapcaplet, pero
la integración HTTP/TLS, el DOM puente, el layout, los plotters y la ventana son
implementaciones específicas de BlesKernOS. Para llamarlo fork de NetSurf
debería conservar y modificar una parte sustancial del núcleo oficial del
navegador, cosa que este frontend todavía no hace.

## Recursos web

- HTTP y HTTPS, redirecciones, cookies, GET/POST, gzip y chunked;
- CSS de `<style>`, atributos `style`, `<link rel="stylesheet">` y `@import`;
- detección y descarga de imágenes referenciadas por HTML y `url(...)` en CSS;
- PNG, GIF, BMP y JPEG baseline;
- `background-image` con `repeat`, `repeat-x`, `repeat-y` y `no-repeat`;
- caché LRU de sesión para CSS, imágenes y respuestas negativas 404;
- deduplicación de recursos repetidos por URL;
- relayout al redimensionar sin volver a descargar los recursos.

Los recursos siguen limitados en cantidad y tamaño para proteger la memoria del
sistema. Una página puede conservar hasta 12 hojas CSS y 24 imágenes, con un
máximo total aproximado de 900.000 píxeles decodificados.

## Layout y CSS

- flujo block, inline e `inline-block`;
- flexbox básico en filas y columnas;
- `justify-content` y `align-items` para los casos más comunes;
- `float: left/right` y `clear` con texto rodeando la caja flotante;
- `position: relative`, `absolute` y `fixed` básico;
- bloques centrados con márgenes automáticos;
- `box-sizing: content-box` y `border-box`;
- `white-space: nowrap` y `pre`;
- anchos porcentuales y `min-width`/`max-width`;
- márgenes y padding porcentuales respecto al bloque contenedor;
- `text-align: left`, `center` y `right`;
- `line-height` numérico y por longitud;
- fondos, bordes, padding y márgenes;
- tablas básicas, incluido reparto de ancho para `colspan`;
- numeración de listas ordenadas;
- extensión final del documento calculada también con cajas posicionadas;
- hasta 1.800 elementos de dibujo y 48 KiB de texto por layout.

El layout cede CPU periódicamente durante selección CSS y construcción de
cajas, para que el hilo de carga no monopolice el sistema.

No pretende ser todavía una implementación completa de CSS2/CSS3. Flex grow y
shrink, grid-template, `rowspan`, z-index, transforms y el algoritmo completo de
tablas siguen siendo aproximaciones o no están disponibles.

## Formularios y navegación

- `input`, `textarea`, botones, checkbox y radio;
- `<select>` y `<option>`, incluida la opción `selected` y opciones desactivadas;
- click en un selector para pasar a la siguiente opción disponible;
- envío GET/POST con el valor elegido;
- navegación a fragmentos `#id` y anclas `<a name>`;
- navegación entre fragmentos de la misma página sin repetir el fetch;
- historial, Atrás/Adelante, Recargar, Inicio, Ctrl+L y F5;
- scrollbar vertical con flechas, pista y arrastre.

## Texto latino

El parser conserva caracteres latinos de ISO-8859-1 en vez de sustituirlos por
ASCII. La fuente 8×8 global ahora contiene glifos para á, é, í, ó, ú, ü, ñ, ç,
mayúsculas equivalentes, ¿, ¡ y ©. Esto permite mostrar textos como “Imágenes”
y “Búsqueda” directamente en las aplicaciones del sistema.

## API pública 13

La ABI pública sube a la versión 13 y agrega:

```c
bool bk_gui_image_decode_jpeg(
    bk_gui_image_t *image,
    const void *data,
    uint32_t length
);
```

El decodificador está basado en NanoJPEG 1.3.5 y admite JPEG baseline en escala
de grises o YCbCr, con subsampling y restart intervals. Los JPEG progresivos,
lossless, CMYK y aritméticos todavía no están soportados.

## Compilación

```sh
make netsurf
```

Como cambian el kernel, la API pública y el decodificador global de imágenes,
para generar una imagen arrancable hay que reconstruir todo:

```sh
make clean
make -j$(nproc)
```

## Límites actuales

- no hay JavaScript;
- JPEG progresivo, WebP y GIF animado no están soportados; SVG es parcial;
- `<select>` usa selección cíclica y todavía no abre una lista desplegable;
- no hay carga de archivos ni formularios multipart;
- hay caché LRU de sesión, pero no caché HTTP persistente ni caché de disco;
- el fetch se ejecuta en un hilo cooperativo, pero cada socket sigue siendo bloqueante;
- Brotli y `Content-Encoding: deflate` HTTP no están disponibles;
- los documentos y resultados descomprimidos están limitados a unos 128 KiB;
- el layout moderno de sitios dependientes de flexbox/grid puede degradarse.

El siguiente paso recomendado es hacer cancelable la lectura TCP por tramos y
añadir revalidación HTTP con ETag/Last-Modified. Después conviene decidir si
continuar ampliando este layout ligero o conectar el frontend al núcleo
completo de NetSurf.


## Stage 17: plotter and proportional text bridge

Document painting now passes through a dedicated BlesKernOS plotter backend.
Web text uses raw proportional metrics, CSS pixel sizes, bold, italic and
monospace rendering. The current display-list renderer remains as a safe
fallback while upstream NetSurf box rendering is connected incrementally.

## Etapa 19: compatibilidad moderna

La etapa 19 amplía el frontend sin introducir JavaScript:

- imágenes responsivas mediante `srcset`, `sizes` y `<picture>`;
- carga de `data-src`, `data-original`, `data-lazy-src`, `video poster` y preloads;
- imágenes `data:image/...;base64`;
- contenido `<noscript>` visible, ya que BlesKernOS no ejecuta scripts;
- `letter-spacing`, `word-spacing`, `text-transform`, `text-indent`, `opacity`
  aproximada y `background-position`;
- `gap`, `order` y `align-self` básicos en flexbox;
- límites mayores para HTML, CSS, imágenes, formularios y display list.

El renderer sigue siendo el layout compacto de BlesKernOS. No implementa todavía
JavaScript, SVG completo, JPEG progresivo, CSS Grid completo ni el algoritmo de
cajas upstream completo de NetSurf.

## Etapa 23: CSS vacío y layout HTML clásico

- las respuestas `2xx` vacías con MIME `text/css` se conservan como hojas
  válidas y se finalizan en libcss, como hace el manejador de contenido de
  NetSurf upstream;
- `<center>` y el atributo heredado `align` vuelven a aportar alineación al
  layout, algo importante en las variantes sin JavaScript de varios sitios;
- las tablas con columnas porcentuales reservan primero el ancho intrínseco de
  las columnas flexibles, en lugar de dividir siempre cada fila en partes
  iguales;
- el cálculo intrínseco de formularios ignora controles ocultos y considera el
  atributo `size` de los campos de texto.

## Etapa 24: presentación y navegación

- toolbar más alto y visualmente separado del documento, con márgenes propios
  para que la dirección no se funda con el contenido;
- botón Detener restaurado y tecla Escape para cancelar una carga;
- Ctrl+K funciona como acceso alternativo a la barra de dirección;
- tipografía web y formularios ajustados a escalas enteras de la fuente bitmap,
  evitando el remuestreo irregular de 10 y 12 píxeles;
- tablas heredadas conservan un mínimo basado en el contenido para que enlaces
  laterales no terminen apilados letra por letra.

## Etapa 25: rendimiento de páginas pesadas

- negociación y decodificación de gzip también para respuestas `chunked`;
- conexiones HTTPS keep-alive delimitadas por `Content-Length` o chunked, con
  reintento automático si el servidor cerró una sesión ociosa;
- pool BearSSL de dos conexiones y descarga de dos hojas CSS en paralelo;
- caché LRU de sesión limitada a 12 objetos, 2 MiB totales y 384 KiB por objeto;
- el DOM de Hubbub se conserva: descubrimiento, layout final y resize ya no
  vuelven a parsear el HTML;
- las hojas externas ya compiladas por libcss se reutilizan en los relayouts;
- una línea `[NETSURF][PERF]` informa tiempo total, red, HTML, CSS, imágenes,
  layout, cantidad de requests y aciertos de caché.

## Etapa 26: compatibilidad visual moderna

- Las hojas pasan por una capa de compatibilidad antes de libcss: conserva
  propiedades personalizadas, resuelve `var(--nombre, fallback)` y simplifica
  expresiones `calc()` de longitudes. Las mezclas de `%`, `px`, `vw`, `vh`,
  `em` y `rem` se convierten usando el viewport actual.
- `display: grid` deja de comportarse como una sola fila Flex cuando la hoja
  contiene `grid-template-columns`. Se reconocen `repeat(N, ...)` y listas de
  hasta 12 tracks; se generan anchos, wrapping y `gap` compatibles con libcss.
- El texto respeta tamaños CSS intermedios (ya no salta solamente entre
  8/16/24 px) y el plotter bitmap mezcla cobertura en los bordes para reducir
  dientes y bloques al escalar.
- El cargador de imágenes reconoce SVG externos. El rasterizador inicial
  soporta `viewBox`, `rect`, `circle`, `ellipse` y paths con M/L/H/V/C/Z. SVG
  inline, filtros, máscaras, texto SVG y comandos de arco quedan pendientes.

Esta etapa mejora sitios que dependen de tokens CSS, tarjetas Grid e iconos
vectoriales, pero no equivale todavía al layout completo de NetSurf upstream:
la cascada de variables con scopes complejos se aproxima por documento y Grid
avanzado (`minmax`, auto-placement detallado y spans) sigue siendo parcial.

## Etapa 27: planificador de recursos y red concurrente

- El planificador descarga hasta cuatro hojas CSS o imágenes al mismo tiempo,
  en vez de bloquear la página por cada recurso. El límite es deliberadamente
  menor que el valor de escritorio de NetSurf porque BlesKernOS tiene menos
  memoria, un único procesador virtual y un stack TCP todavía pequeño.
- BearSSL conserva hasta cuatro sesiones HTTPS keep-alive y el stack dispone de
  ocho sockets TCP, dejando margen para navegación, DNS y conexiones en cierre.
- La caché LRU de recursos sube a 32 objetos y 8 MiB, con un máximo de 512 KiB
  por objeto. CSS e imágenes visitados durante la sesión evitan otra descarga.
- El resolvedor mantiene una caché DNS de ocho hosts. También queda serializado:
  su estado interno es global y las consultas simultáneas podían corromperse.
- La asignación de sockets ahora reserva el slot bajo un lock. Antes dos workers
  podían recibir el mismo descriptor, pisar su estado TCP y provocar esperas,
  reintentos o recursos incompletos.
- La métrica de CSS usa el tiempo real de pared del lote concurrente y no suma
  los tiempos de cada worker, por lo que `[NETSURF][PERF]` vuelve a representar
  correctamente lo que percibe el usuario.

Todavía no es una copia del subsistema completo de NetSurf: upstream combina
una cola global de fetches, límites por host, curl-multi y una low-level cache
con caducidad y revalidación HTTP. En este port los `@import` y las imágenes
descubiertas dentro del CSS aún se procesan en serie, y la caché dura solamente
lo que dura el proceso.
