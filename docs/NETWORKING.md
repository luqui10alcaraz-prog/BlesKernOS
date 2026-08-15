# Red IPv4, RTL8139, sockets y web

La red de BlesKernOS 0.8 se divide en módulos ELF32 externos:

- `3C90X.DVR`: placas 3Com EtherLink XL 3c90x.
- `RTL8139.DVR`: Realtek RTL8129/RTL8139 PCI y clones compatibles.
- `NETSTACK.DVR`: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP y HTTP.
- `TLS.DVR`: cliente TLS 1.2 BearSSL, validación X.509 y HTTPS.

El kernel sólo conserva el registro de NIC, los proxies de `network.c` y las
syscalls. La pila no conoce registros PCI y los proveedores TLS no conocen el
hardware. Por eso puede sustituirse una NIC sin recompilar los protocolos.
DHCP se inicia automáticamente después de cargar los módulos.

## Uso desde la shell

```text
ifconfig                 # alias de ipconfig
ipconfig
net dhcp
net static 192.168.1.20 255.255.255.0 192.168.1.1 1.1.1.1
ping 1.1.1.1
ping example.com
dns example.com
tcp example.com 80
httphead https://example.com/
nettest
net test example.com
curl http://example.com/
curl https://example.com/
wget https://example.com/ pagina.html
```

`curl` muestra el cuerpo y `wget` descarta las cabeceras antes de guardar. El
cliente HTTP envía HTTP/1.1, `Host`, SNI en HTTPS y `Connection: close`. El
buffer actual limita cada respuesta a 64 KiB y todavía no decodifica
`Transfer-Encoding: chunked`, redirecciones ni compresión.

`ipconfig` diferencia una NIC meramente visible en PCI de una NIC realmente
registrada en la pila: muestra el nombre (`eth0-rtl8139`), enlace, MTU, módulos,
MAC y contadores. `nettest` ejecuta DHCP si hace falta y prueba gateway, DNS,
handshake TCP, HTTP y HTTPS. Al final exige que RX y TX hayan aumentado; esa
diferencia de contadores demuestra que las tramas atravesaron las funciones
del driver indicado y no sólo que Device Manager encontró su ID PCI.

## RTL8139

El controlador reconoce `10EC:8129`, `10EC:8138` y `10EC:8139`, además de una
lista pequeña de IDs compatibles. Activa bus mastering, usa BAR0 de E/S, el
anillo RX de 8 KiB + margen de wrap documentado por Realtek y cuatro buffers
TX. La recepción se sondea desde una tarea para no depender todavía de IRQ PCI
compartidas.

Prueba reproducible en QEMU:

```sh
make
make run-net
```

El arranque debe mostrar `[RTL8139]`, luego una concesión DHCP habitual de
SLiRP (`10.0.2.15`, router `10.0.2.2`, DNS `10.0.2.3`). El objetivo `run-net`
usa `-cpu qemu32,+rdrand`, requerido por TLS.

## Sockets para aplicaciones Ring 3

La syscall ABI 3 ofrece sockets TCP cliente. Cada proceso tiene hasta cuatro
handles propios; el kernel valida los buffers y cierra automáticamente todos
los sockets cuando el proceso termina.

```c
#include <bleskernos.h>

bk_u8 ip[4];
bk_i32 s = bk_socket(BK_SOCKET_TCP);
if (s >= 0 && bk_dns_resolve("example.com", ip, 5000) == 0 &&
    bk_connect(s, ip, 80, 5000) == 0) {
    static const char request[] =
        "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    char response[1024];
    bk_send(s, request, sizeof(request) - 1, 5000);
    bk_recv(s, response, sizeof(response), 5000);
}
bk_socket_close(s);
```

`bk_recv()` devuelve `0` al vencer el timeout o al cerrar el peer, un número
positivo de bytes, o un error negativo de syscall.
Para una descarga directa, `bk_http_get(url, buffer, capacidad, timeout)`
acepta tanto HTTP como HTTPS y devuelve la respuesta completa, incluidas las
cabeceras.

## Seguridad TLS

`TLS.DVR` contiene BearSSL 0.6 y un bundle de 121 autoridades generado desde
el bundle Mozilla publicado por curl. Verifica la cadena, firma, vigencia y
nombre DNS; usa SNI y rechaza certificados desconocidos o una fecha RTC no
válida. No desactiva la validación para “hacer funcionar” HTTPS.

El código importado de BearSSL está en `libs/bearssl`, junto con su licencia.
No puede borrarse sin reemplazar TLS por otra biblioteca o implementar toda la
criptografía necesaria; se retiró el directorio genérico `third_party` para
integrarlo con las demás bibliotecas externas del proyecto.

El módulo exige RDRAND y mezcla 256 bits en el PRNG de BearSSL. Si la CPU no
ofrece esa instrucción, HTTPS falla con el error local `1007`; usar ticks o la
hora como sustituto sería predecible y no sería TLS seguro. BearSSL implementa
TLS 1.2, no TLS 1.3. El bundle de CA debe regenerarse periódicamente desde una
fuente confiable y la hora CMOS debe ser correcta.

## Límites actuales de TCP/IP

- IPv4 solamente, una NIC y una entrada ARP activa.
- Sin fragmentación/reensamblado IPv4 ni API UDP pública.
- TCP cliente con retransmisión básica, secuencias, ACK y FIN; sin escucha,
  control de congestión, SACK, window scaling ni reensamblado fuera de orden.
- Cuatro sockets y 8192 bytes RX por socket.
- El sondeo de NIC y buffers fijos priorizan claridad y bring-up; no rendimiento.

Antes de usarlo para datos sensibles hacen falta pruebas de interoperabilidad,
fuzzing del procesamiento de paquetes, más entropía del sistema, actualización
automatizada de CA y una implementación TCP más completa.
