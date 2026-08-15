#!/usr/bin/env python3
"""Generate BlesKernOS .LNG packs from user-visible source strings.

The runtime keeps named keys for new code and FNV-1a hash keys for old apps.
This lets every existing native program use external language packs without a
flag-day rewrite; new code should prefer bk_lang_get("SECTION.KEY").
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "system" / "leng"
MANIFEST = OUT / "SOURCES.TXT"

NAMED = {
    "ES": {
        "COMMON.READY": "Listo",
        "COMMON.OK": "Aceptar",
        "COMMON.CANCEL": "Cancelar",
        "COMMON.APPLY": "Aplicar",
        "COMMON.CLOSE": "Cerrar",
        "COMMON.YES": "Si",
        "COMMON.NO": "No",
        "COMMON.ERROR": "Error",
        "COMMON.VERSION_1": "Version 1.0",
        "LANGUAGE.TITLE": "Idioma",
        "LANGUAGE.GROUP": "Idioma de la interfaz",
        "LANGUAGE.DESCRIPTION": "Seleccione el idioma usado por BlesKernOS y sus programas.",
        "LANGUAGE.CURRENT": "Idioma actual: %s",
        "LANGUAGE.LIVE_NOTE": "El cambio se aplica inmediatamente a las ventanas abiertas.",
        "LANGUAGE.FILES_GROUP": "Archivos de idioma",
        "LANGUAGE.FILES_PATH": "Los catalogos se cargan desde /SYSTEM/LENG/*.LNG.",
        "LANGUAGE.RESTART_NOTE": "Algunos programas pueden requerir volver a abrirse.",
        "LANGUAGE.SPANISH": "Espanol",
        "LANGUAGE.ENGLISH": "Ingles",
        "LANGUAGE.ITALIAN": "Italiano",
        "LANGUAGE.CHANGED": "Idioma cambiado a %s.",
        "LANGUAGE.ERROR": "No se pudo cargar el archivo de idioma.",
        "LANGUAGE.ABOUT": "Seleccion de idioma para BlesKernOS.",
    },
    "EN": {
        "COMMON.READY": "Ready",
        "COMMON.OK": "OK",
        "COMMON.CANCEL": "Cancel",
        "COMMON.APPLY": "Apply",
        "COMMON.CLOSE": "Close",
        "COMMON.YES": "Yes",
        "COMMON.NO": "No",
        "COMMON.ERROR": "Error",
        "COMMON.VERSION_1": "Version 1.0",
        "LANGUAGE.TITLE": "Language",
        "LANGUAGE.GROUP": "Interface language",
        "LANGUAGE.DESCRIPTION": "Select the language used by BlesKernOS and its programs.",
        "LANGUAGE.CURRENT": "Current language: %s",
        "LANGUAGE.LIVE_NOTE": "The change is applied immediately to open windows.",
        "LANGUAGE.FILES_GROUP": "Language files",
        "LANGUAGE.FILES_PATH": "Catalogs are loaded from /SYSTEM/LENG/*.LNG.",
        "LANGUAGE.RESTART_NOTE": "Some programs may need to be reopened.",
        "LANGUAGE.SPANISH": "Spanish",
        "LANGUAGE.ENGLISH": "English",
        "LANGUAGE.ITALIAN": "Italian",
        "LANGUAGE.CHANGED": "Language changed to %s.",
        "LANGUAGE.ERROR": "The language file could not be loaded.",
        "LANGUAGE.ABOUT": "Language selection for BlesKernOS.",
    },
    "IT": {
        "COMMON.READY": "Pronto",
        "COMMON.OK": "OK",
        "COMMON.CANCEL": "Annulla",
        "COMMON.APPLY": "Applica",
        "COMMON.CLOSE": "Chiudi",
        "COMMON.YES": "Si",
        "COMMON.NO": "No",
        "COMMON.ERROR": "Errore",
        "COMMON.VERSION_1": "Versione 1.0",
        "LANGUAGE.TITLE": "Lingua",
        "LANGUAGE.GROUP": "Lingua dell'interfaccia",
        "LANGUAGE.DESCRIPTION": "Seleziona la lingua usata da BlesKernOS e dai suoi programmi.",
        "LANGUAGE.CURRENT": "Lingua attuale: %s",
        "LANGUAGE.LIVE_NOTE": "La modifica viene applicata subito alle finestre aperte.",
        "LANGUAGE.FILES_GROUP": "File di lingua",
        "LANGUAGE.FILES_PATH": "I cataloghi vengono caricati da /SYSTEM/LENG/*.LNG.",
        "LANGUAGE.RESTART_NOTE": "Alcuni programmi potrebbero dover essere riaperti.",
        "LANGUAGE.SPANISH": "Spagnolo",
        "LANGUAGE.ENGLISH": "Inglese",
        "LANGUAGE.ITALIAN": "Italiano",
        "LANGUAGE.CHANGED": "Lingua cambiata in %s.",
        "LANGUAGE.ERROR": "Impossibile caricare il file di lingua.",
        "LANGUAGE.ABOUT": "Selezione della lingua per BlesKernOS.",
    },
}

EN_EXACT = {
    "Sobre BlesKernOS 1.0": "About BlesKernOS 1.0",
    "Calculadora": "Calculator", "Calendario": "Calendar",
    "Archivos": "Files", "Juegos": "Games", "Pintar": "Paint",
    "Administrador de procesos": "Process Manager",
    "Panel de control": "Control Panel", "Ejecutar": "Run",
    "Editor": "Editor", "Visor de imagenes": "Image Viewer",
    "Visor de imagenes.": "Image viewer.", "ScanDisk": "ScanDisk",
    "Pantalla": "Display", "Sonido": "Sound", "Fecha y hora": "Date and Time",
    "Mouse": "Mouse", "Teclado": "Keyboard", "Sistema": "System",
    "Dispositivos": "Devices", "Internet": "Internet", "Modem": "Modem",
    "Idioma": "Language", "Apariencia": "Appearance",
    "Enero": "January", "Febrero": "February", "Marzo": "March",
    "Abril": "April", "Mayo": "May", "Junio": "June", "Julio": "July",
    "Agosto": "August", "Septiembre": "September", "Octubre": "October",
    "Noviembre": "November", "Diciembre": "December",
    "Dom": "Sun", "Lun": "Mon", "Mar": "Tue", "Mie": "Wed",
    "Jue": "Thu", "Vie": "Fri", "Sab": "Sat", "Hoy": "Today",
    "Hoy: ": "Today: ", "RTC sin fecha valida": "RTC has no valid date",
    "Disquete": "Floppy disk", "Disco local": "Local Disk",
    "Este equipo": "My Computer", "unidad": "drive",
    "General": "General", "Detalles": "Details", "Nombre:": "Name:",
    "Tipo:": "Type:", "Carpeta": "Folder", "Archivo": "File",
    "Ubicacion:": "Location:", "Tamano:": "Size:", "Volumen:": "Volume:",
    "Solo lectura:": "Read-only:", "Oculto:": "Hidden:", "Si": "Yes", "No": "No",
    "Sin montar": "Not mounted", "Propiedades": "Properties",
    "Propiedades: ": "Properties: ", "Subir": "Up", "Eliminar": "Delete",
    "Cancelar": "Cancel", "Abrir": "Open", "Copiar": "Copy", "Cortar": "Cut",
    "Renombrar": "Rename", "Pegar": "Paste", "Nueva carpeta": "New folder",
    "Actualizar": "Refresh", "Crear carpeta": "Create folder", "Refrescar": "Refresh",
    "Iconos": "Icons", "Lista": "List", "Archivo": "File", "Editar": "Edit",
    "Ver": "View", "Ayuda": "Help", "Acerca de Archivos": "About Files",
    "Ruta no encontrada": "Path not found", "Elemento eliminado": "Item deleted",
    "Carpeta creada": "Folder created", "Elemento renombrado": "Item renamed",
    "Elemento movido": "Item moved", "Elemento pegado": "Item pasted",
    "Actualizado": "Updated", "Controlador cargado": "Driver loaded",
    "La unidad no esta lista": "The drive is not ready",
    "No hay una aplicacion asociada": "No application is associated",
    "Desea eliminar este elemento?": "Do you want to delete this item?",
    "Esta accion no se puede deshacer.": "This action cannot be undone.",
    "Confirmar eliminacion": "Confirm deletion",
    "Enter = Aceptar   Esc = Cancelar": "Enter = OK   Esc = Cancel",
    "Gana rojo!": "Red wins!", "Gana amarillo!": "Yellow wins!",
    "Turno: rojo": "Turn: red", "Turno: amarillo": "Turn: yellow",
    "Reiniciar": "Restart", "Pausa": "Pause", "Play": "Play",
    "Ajustar": "Fit", "Reproduciendo": "Playing", "Detenido": "Stopped",
    "Reanudando": "Resuming", "Inicializando...": "Initializing...",
    "Fin de playlist": "End of playlist", "sin archivo": "no file",
    "Nuevo": "New", "Guardar": "Save", "Guardar BMP": "Save BMP",
    "Lapiz": "Pencil", "Goma": "Eraser", "Cubeta": "Fill",
    "Linea": "Line", "Rectangulo": "Rectangle", "Nuevo dibujo": "New drawing",
    "Canvas limpio": "Canvas cleared", "Color seleccionado": "Color selected",
    "Dibujando": "Drawing", "Colores": "Colors", "Herramienta": "Tool",
    "Finalizar": "End task", "PROCESO": "PROCESS", "ESTADO": "STATE",
    "Tareas: ": "Tasks: ", "CPU sistema: ": "System CPU: ",
    "RAM sistema: ": "System RAM: ", "Listo": "Ready",
    "Escribi una ruta o programa": "Type a path or program",
    "Abrir configuracion": "Open settings", "Informacion": "Information",
    "Prueba del teclado": "Keyboard test", "Escriba aqui:": "Type here:",
    "Distribucion preferida": "Preferred layout", "Seleccion actual: %s": "Current selection: %s",
    "24 horas": "24-hour", "12 horas": "12-hour", "Formato": "Format",
    "Fecha": "Date", "Hora": "Time", "Zona horaria": "Time zone",
    "Aplicar": "Apply", "Cerrar": "Close", "Aceptar": "OK",
    "Version 1.0": "Version 1.0", "Version 1.1": "Version 1.1",
    "Fondo, protector y resolucion": "Wallpaper, screen saver and resolution",
    "Dispositivos de audio": "Audio devices", "Reloj del sistema": "System clock",
    "Estado y sensibilidad": "Status and sensitivity", "Distribucion y prueba": "Layout and test",
    "Informacion del equipo": "Computer information", "Administrador de hardware": "Hardware manager",
    "Navegacion, contenido y conexion": "Browsing, content and connection",
    "Deteccion y preferencias de marcacion": "Detection and dialing preferences",
    "Idioma de la interfaz y archivos LNG": "Interface language and LNG files",
    "Seleccione un icono para configurar BlesKernOS.": "Select an icon to configure BlesKernOS.",
    "Configuracion de BlesKernOS.": "BlesKernOS settings.",
    "Calculadora de BlesKernOS.": "BlesKernOS calculator.",
    "Calendario de BlesKernOS.": "BlesKernOS calendar.",
    "Administrador de archivos de BlesKernOS.": "BlesKernOS file manager.",
    "Centro de juegos de BlesKernOS.": "BlesKernOS game center.",
    "Demostracion grafica OpenGL.": "OpenGL graphics demonstration.",
    "Reproductor MIDI y WAV de BlesKernOS.": "BlesKernOS MIDI and WAV player.",
    "Editor de imagenes BMP de BlesKernOS.": "BlesKernOS BMP image editor.",
    "Procesos y uso de memoria del sistema.": "System processes and memory usage.",
    "Lanzador de programas y comandos.": "Program and command launcher.",
    "Editor de texto de BlesKernOS.": "BlesKernOS text editor.",
    "Consola de comandos de BlesKernOS.": "BlesKernOS command console.",
    "No se pudo abrir la imagen": "Could not open the image",
    "No se pudo abrir el explorador": "Could not open the file browser",
    "No se pudo leer el MIDI": "Could not read the MIDI file",
    "MIDI invalido o no soportado": "Invalid or unsupported MIDI",
    "No se pudo crear BMP en memoria": "Could not create BMP in memory",
    "Sin memoria para canvas": "Not enough memory for the canvas",
    "Sin memoria": "Out of memory", "Sin espacio": "No space left",
    "No hay seleccion": "No selection", "Seleccion copiada": "Selection copied",
    "Portapapeles vacio": "Clipboard is empty", "Pegado": "Pasted",
    "Todo seleccionado": "All selected", "Nuevo archivo": "New file",
    "Guardado": "Saved", "Error al guardar": "Save error",
    "Cargando...": "Loading...", "Conectando con el sitio...": "Connecting to site...",
    "Buscando servidor...": "Looking up server...", "Pagina lista": "Page ready",
    "Preparando navegacion...": "Preparing navigation...",
    "Atras": "Back", "Adelante": "Forward", "Recargar": "Reload", "Inicio": "Home",
    "Ir": "Go", "Buscar": "Search", "Imagenes": "Images",
    "Privacidad": "Privacy", "Condiciones": "Terms",
    "Analizar": "Analyze", "Confirmar": "Confirm", "Reparar": "Repair",
    "Confirmacion cancelada.": "Confirmation cancelled.",
    "El volumen no presenta errores detectables.": "No detectable errors were found on the volume.",
    "Se encontraron problemas. Revise el informe.": "Problems were found. Review the report.",
    "Reparacion completada y volumen verificado.": "Repair completed and volume verified.",
}

IT_EXACT = {
    "Sobre BlesKernOS 1.0": "Informazioni su BlesKernOS 1.0",
    "Calculadora": "Calcolatrice", "Calendario": "Calendario", "Archivos": "File",
    "Juegos": "Giochi", "Administrador de procesos": "Gestione processi",
    "Panel de control": "Pannello di controllo", "Ejecutar": "Esegui",
    "Editor": "Editor", "Visor de imagenes": "Visualizzatore immagini",
    "Pantalla": "Schermo", "Sonido": "Audio", "Fecha y hora": "Data e ora",
    "Mouse": "Mouse", "Teclado": "Tastiera", "Sistema": "Sistema",
    "Dispositivos": "Dispositivi", "Internet": "Internet", "Modem": "Modem",
    "Idioma": "Lingua", "Apariencia": "Aspetto",
    "Enero": "Gennaio", "Febrero": "Febbraio", "Marzo": "Marzo",
    "Abril": "Aprile", "Mayo": "Maggio", "Junio": "Giugno", "Julio": "Luglio",
    "Agosto": "Agosto", "Septiembre": "Settembre", "Octubre": "Ottobre",
    "Noviembre": "Novembre", "Diciembre": "Dicembre",
    "Dom": "Dom", "Lun": "Lun", "Mar": "Mar", "Mie": "Mer",
    "Jue": "Gio", "Vie": "Ven", "Sab": "Sab", "Hoy": "Oggi", "Hoy: ": "Oggi: ",
    "RTC sin fecha valida": "RTC senza data valida", "Disquete": "Dischetto",
    "Disco local": "Disco locale", "Este equipo": "Risorse del computer", "unidad": "unita",
    "General": "Generale", "Detalles": "Dettagli", "Nombre:": "Nome:",
    "Tipo:": "Tipo:", "Carpeta": "Cartella", "Archivo": "File",
    "Ubicacion:": "Posizione:", "Tamano:": "Dimensione:", "Volumen:": "Volume:",
    "Solo lectura:": "Sola lettura:", "Oculto:": "Nascosto:", "Si": "Si", "No": "No",
    "Sin montar": "Non montato", "Propiedades": "Proprieta", "Propiedades: ": "Proprieta: ",
    "Subir": "Su", "Eliminar": "Elimina", "Cancelar": "Annulla", "Abrir": "Apri",
    "Copiar": "Copia", "Cortar": "Taglia", "Renombrar": "Rinomina", "Pegar": "Incolla",
    "Nueva carpeta": "Nuova cartella", "Actualizar": "Aggiorna", "Crear carpeta": "Crea cartella",
    "Refrescar": "Aggiorna", "Iconos": "Icone", "Lista": "Elenco",
    "File": "File", "Edit": "Modifica", "View": "Visualizza", "Help": "Aiuto",
    "Acerca de Archivos": "Informazioni su File", "Ruta no encontrada": "Percorso non trovato",
    "Elemento eliminado": "Elemento eliminato", "Carpeta creada": "Cartella creata",
    "Elemento renombrado": "Elemento rinominato", "Elemento movido": "Elemento spostato",
    "Elemento pegado": "Elemento incollato", "Actualizado": "Aggiornato",
    "Controlador cargado": "Driver caricato", "La unidad no esta lista": "L'unita non e pronta",
    "No hay una aplicacion asociada": "Nessuna applicazione associata",
    "Desea eliminar este elemento?": "Eliminare questo elemento?",
    "Esta accion no se puede deshacer.": "Questa azione non puo essere annullata.",
    "Confirmar eliminacion": "Conferma eliminazione",
    "Enter = Aceptar   Esc = Cancelar": "Invio = OK   Esc = Annulla",
    "Gana rojo!": "Vince il rosso!", "Gana amarillo!": "Vince il giallo!",
    "Turno: rojo": "Turno: rosso", "Turno: amarillo": "Turno: giallo",
    "Reiniciar": "Ricomincia", "Pausa": "Pausa", "Play": "Riproduci",
    "Ajustar": "Adatta", "Reproduciendo": "Riproduzione", "Detenido": "Fermato",
    "Reanudando": "Ripresa", "Inicializando...": "Inizializzazione...",
    "Fin de playlist": "Fine playlist", "sin archivo": "nessun file",
    "Nuevo": "Nuovo", "Guardar": "Salva", "Guardar BMP": "Salva BMP",
    "Lapiz": "Matita", "Goma": "Gomma", "Cubeta": "Riempimento",
    "Linea": "Linea", "Rectangulo": "Rettangolo", "Nuevo dibujo": "Nuovo disegno",
    "Canvas limpio": "Area pulita", "Color seleccionado": "Colore selezionato",
    "Dibujando": "Disegno", "Colores": "Colori", "Herramienta": "Strumento",
    "Finalizar": "Termina", "PROCESO": "PROCESSO", "ESTADO": "STATO",
    "Tareas: ": "Processi: ", "CPU sistema: ": "CPU sistema: ",
    "RAM sistema: ": "RAM sistema: ", "Listo": "Pronto",
    "Escribi una ruta o programa": "Scrivi un percorso o programma",
    "Abrir configuracion": "Apri impostazioni", "Informacion": "Informazioni",
    "Prueba del teclado": "Prova tastiera", "Escriba aqui:": "Scrivi qui:",
    "Distribucion preferida": "Layout preferito", "Seleccion actual: %s": "Selezione attuale: %s",
    "24 horas": "24 ore", "12 horas": "12 ore", "Formato": "Formato",
    "Fecha": "Data", "Hora": "Ora", "Zona horaria": "Fuso orario",
    "Aplicar": "Applica", "Cerrar": "Chiudi", "Aceptar": "OK",
    "Version 1.0": "Versione 1.0", "Version 1.1": "Versione 1.1",
    "Fondo, protector y resolucion": "Sfondo, salvaschermo e risoluzione",
    "Dispositivos de audio": "Dispositivi audio", "Reloj del sistema": "Orologio di sistema",
    "Estado y sensibilidad": "Stato e sensibilita", "Distribucion y prueba": "Layout e prova",
    "Informacion del equipo": "Informazioni sul computer", "Administrador de hardware": "Gestione hardware",
    "Navegacion, contenido y conexion": "Navigazione, contenuti e connessione",
    "Deteccion y preferencias de marcacion": "Rilevamento e preferenze di composizione",
    "Idioma de la interfaz y archivos LNG": "Lingua dell'interfaccia e file LNG",
    "Seleccione un icono para configurar BlesKernOS.": "Seleziona un'icona per configurare BlesKernOS.",
    "Configuracion de BlesKernOS.": "Configurazione di BlesKernOS.",
    "Calculadora de BlesKernOS.": "Calcolatrice di BlesKernOS.",
    "Calendario de BlesKernOS.": "Calendario di BlesKernOS.",
    "Administrador de archivos de BlesKernOS.": "Gestione file di BlesKernOS.",
    "Centro de juegos de BlesKernOS.": "Centro giochi di BlesKernOS.",
    "Demostracion grafica OpenGL.": "Dimostrazione grafica OpenGL.",
    "Reproductor MIDI y WAV de BlesKernOS.": "Lettore MIDI e WAV di BlesKernOS.",
    "Editor de imagenes BMP de BlesKernOS.": "Editor di immagini BMP di BlesKernOS.",
    "Procesos y uso de memoria del sistema.": "Processi e uso della memoria di sistema.",
    "Lanzador de programas y comandos.": "Avvio di programmi e comandi.",
    "Editor de texto de BlesKernOS.": "Editor di testo di BlesKernOS.",
    "Consola de comandos de BlesKernOS.": "Console dei comandi di BlesKernOS.",
    "No se pudo abrir la imagen": "Impossibile aprire l'immagine",
    "No se pudo abrir el explorador": "Impossibile aprire il gestore file",
    "No se pudo leer el MIDI": "Impossibile leggere il MIDI",
    "MIDI invalido o no soportado": "MIDI non valido o non supportato",
    "No se pudo crear BMP en memoria": "Impossibile creare il BMP in memoria",
    "Sin memoria para canvas": "Memoria insufficiente per l'area di disegno",
    "Sin memoria": "Memoria insufficiente", "Sin espacio": "Spazio insufficiente",
    "No hay seleccion": "Nessuna selezione", "Seleccion copiada": "Selezione copiata",
    "Portapapeles vacio": "Appunti vuoti", "Pegado": "Incollato",
    "Todo seleccionado": "Tutto selezionato", "Nuevo archivo": "Nuovo file",
    "Guardado": "Salvato", "Error al guardar": "Errore di salvataggio",
    "Cargando...": "Caricamento...", "Conectando con el sitio...": "Connessione al sito...",
    "Buscando servidor...": "Ricerca del server...", "Pagina lista": "Pagina pronta",
    "Preparando navegacion...": "Preparazione navigazione...",
    "Atras": "Indietro", "Adelante": "Avanti", "Recargar": "Ricarica", "Inicio": "Home",
    "Ir": "Vai", "Buscar": "Cerca", "Imagenes": "Immagini",
    "Privacidad": "Privacy", "Condiciones": "Termini",
    "Analizar": "Analizza", "Confirmar": "Conferma", "Reparar": "Ripara",
    "Confirmacion cancelada.": "Conferma annullata.",
    "El volumen no presenta errores detectables.": "Il volume non presenta errori rilevabili.",
    "Se encontraron problemas. Revise el informe.": "Sono stati trovati problemi. Controlla il rapporto.",
    "Reparacion completada y volumen verificado.": "Riparazione completata e volume verificato.",
}

# Ordered phrase substitutions for strings not covered exactly.
EN_PHRASES = [
    ("No se pudo ", "Could not "), ("No se puede ", "Cannot "),
    ("No hay ", "There is no "), ("No esta ", "Is not "),
    ("Se pudo ", "Could "), ("Seleccione ", "Select "),
    ("Escriba ", "Type "), ("Escribi ", "Type "),
    ("Pulse ", "Press "), ("Guardando", "Saving"),
    ("Cargando", "Loading"), ("Abriendo", "Opening"),
    ("Reproduciendo", "Playing"), ("Configuracion", "Settings"),
    ("Administrador de ", "Manager: "), ("Informacion de ", "Information about "),
    ("Version ", "Version "), (" de BlesKernOS", " for BlesKernOS"),
    (" del sistema", " of the system"), (" del equipo", " of the computer"),
    (" para ", " for "), (" actual", " current"),
]
IT_PHRASES = [
    ("No se pudo ", "Impossibile "), ("No se puede ", "Non e possibile "),
    ("No hay ", "Non c'e "), ("No esta ", "Non e "),
    ("Seleccione ", "Seleziona "), ("Escriba ", "Scrivi "),
    ("Escribi ", "Scrivi "), ("Pulse ", "Premi "),
    ("Guardando", "Salvataggio"), ("Cargando", "Caricamento"),
    ("Abriendo", "Apertura"), ("Reproduciendo", "Riproduzione"),
    ("Configuracion", "Configurazione"), ("Administrador de ", "Gestione "),
    ("Informacion de ", "Informazioni su "), ("Version ", "Versione "),
    (" de BlesKernOS", " di BlesKernOS"), (" del sistema", " del sistema"),
    (" del equipo", " del computer"), (" para ", " per "),
    (" actual", " attuale"),
]

EN_WORDS = {
    "Error":"Error", "errores":"errors", "advertencias":"warnings", "archivo":"file",
    "archivos":"files", "carpeta":"folder", "carpetas":"folders", "imagen":"image",
    "imagenes":"images", "memoria":"memory", "pantalla":"screen", "ventana":"window",
    "programa":"program", "programas":"programs", "proceso":"process", "procesos":"processes",
    "sistema":"system", "dispositivo":"device", "dispositivos":"devices", "controlador":"driver",
    "unidad":"drive", "volumen":"volume", "fecha":"date", "hora":"time", "sonido":"sound",
    "teclado":"keyboard", "mouse":"mouse", "red":"network", "conexion":"connection",
    "conectando":"connecting", "servidor":"server", "pagina":"page", "listo":"ready",
    "activo":"active", "inactivo":"inactive", "habilitado":"enabled", "deshabilitado":"disabled",
    "guardar":"save", "abrir":"open", "cerrar":"close", "eliminar":"delete", "copiar":"copy",
    "cortar":"cut", "pegar":"paste", "nuevo":"new", "nueva":"new", "nombre":"name",
    "tipo":"type", "estado":"status", "tamano":"size", "seleccion":"selection",
    "seleccionado":"selected", "actual":"current", "preferida":"preferred", "preferido":"preferred",
    "prueba":"test", "informacion":"information", "configuracion":"settings", "propiedades":"properties",
    "fondo":"background", "resolucion":"resolution", "protector":"screen saver", "audio":"audio",
    "reloj":"clock", "navegacion":"browsing", "contenido":"content", "detalles":"details",
    "reparacion":"repair", "analisis":"analysis", "resultado":"result", "completado":"completed",
    "fallo":"failed", "creado":"created", "libres":"free", "total":"total", "usados":"used",
    "aceptar":"OK", "cancelar":"Cancel", "aplicar":"Apply", "si":"Yes", "no":"No",
}
IT_WORDS = {
    "Error":"Errore", "errores":"errori", "advertencias":"avvisi", "archivo":"file",
    "archivos":"file", "carpeta":"cartella", "carpetas":"cartelle", "imagen":"immagine",
    "imagenes":"immagini", "memoria":"memoria", "pantalla":"schermo", "ventana":"finestra",
    "programa":"programma", "programas":"programmi", "proceso":"processo", "procesos":"processi",
    "sistema":"sistema", "dispositivo":"dispositivo", "dispositivos":"dispositivi", "controlador":"driver",
    "unidad":"unita", "volumen":"volume", "fecha":"data", "hora":"ora", "sonido":"audio",
    "teclado":"tastiera", "mouse":"mouse", "red":"rete", "conexion":"connessione",
    "conectando":"connessione", "servidor":"server", "pagina":"pagina", "listo":"pronto",
    "activo":"attivo", "inactivo":"inattivo", "habilitado":"abilitato", "deshabilitado":"disabilitato",
    "guardar":"salva", "abrir":"apri", "cerrar":"chiudi", "eliminar":"elimina", "copiar":"copia",
    "cortar":"taglia", "pegar":"incolla", "nuevo":"nuovo", "nueva":"nuova", "nombre":"nome",
    "tipo":"tipo", "estado":"stato", "tamano":"dimensione", "seleccion":"selezione",
    "seleccionado":"selezionato", "actual":"attuale", "preferida":"preferito", "preferido":"preferito",
    "prueba":"prova", "informacion":"informazioni", "configuracion":"configurazione", "propiedades":"proprieta",
    "fondo":"sfondo", "resolucion":"risoluzione", "protector":"salvaschermo", "audio":"audio",
    "reloj":"orologio", "navegacion":"navigazione", "contenido":"contenuto", "detalles":"dettagli",
    "reparacion":"riparazione", "analisis":"analisi", "resultado":"risultato", "completado":"completato",
    "fallo":"fallito", "creado":"creato", "libres":"liberi", "total":"totale", "usados":"usati",
    "aceptar":"OK", "cancelar":"Annulla", "aplicar":"Applica", "si":"Si", "no":"No",
}

SINGLE_UI = {
    "File", "Edit", "View", "Help", "General", "Detalles", "Abrir", "Copiar",
    "Cortar", "Pegar", "Eliminar", "Cancelar", "Guardar", "Nuevo", "Actualizar",
    "Propiedades", "Listo", "Error", "Hoy", "Pausa", "Play", "Reiniciar",
    "Aplicar", "Aceptar", "Cerrar", "Analizar", "Confirmar", "Reparar",
    "Pantalla", "Sonido", "Mouse", "Teclado", "Sistema", "Dispositivos",
    "Internet", "Modem", "Idioma", "Calculadora", "Calendario", "Archivos",
    "Juegos", "Paint", "Editor", "ScanDisk", "Ejecutar", "Informacion",
}

SOURCE_GLOBS = (
    "programs/*.c", "system/control/*.c", "system/desktop/*.c",
    "system/services/*.c", "system/commands/*.c", "gui/*.c",
    "programs/netsurf/*.c",
)
SOURCE_FILES = ("kernel/about_dialog.c", "kernel/file_dialog.c")


def fnv1a(text: str) -> int:
    value = 2166136261
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value or 1


def c_strings(text: str):
    i, n, state = 0, len(text), "code"
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/": state, i = "line", i + 2; continue
            if c == "/" and i + 1 < n and text[i + 1] == "*": state, i = "block", i + 2; continue
            if c == "'": state, i = "char", i + 1; continue
            if c == '"':
                i += 1; out = []
                while i < n:
                    c = text[i]
                    if c == '"': i += 1; break
                    if c == "\\" and i + 1 < n:
                        esc = text[i + 1]
                        out.append({"n":"\n", "r":"\r", "t":"\t", "0":"\0", '"':'"', "\\":"\\"}.get(esc, esc))
                        i += 2; continue
                    out.append(c); i += 1
                yield "".join(out); continue
            i += 1
        elif state == "line":
            if c == "\n": state = "code"
            i += 1
        elif state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/": state, i = "code", i + 2
            else: i += 1
        else:
            if c == "\\" and i + 1 < n: i += 2
            elif c == "'": state, i = "code", i + 1
            else: i += 1


def visible_candidate(value: str) -> bool:
    if len(value) < 2 or not any(ch.isalpha() for ch in value): return False
    if value.startswith(("../", "/", "http://", "https://", "-I", "-D")): return False
    if re.fullmatch(r"\.?[A-Za-z0-9_/-]+\.(?:c|h|o|a|bmp|gif|png|jpe?g|wav|ini|cpl|scv|dvr|exe|dll|pak|wad|mid|kar|txt|md|asm|elf)", value, re.I): return False
    if value.startswith(("BK", "GUI_", "VFS_", "NET_", "CSS_", "HUBBUB_")): return False
    if "\0" in value: return False
    if not any(ch.isspace() for ch in value) and value not in SINGLE_UI:
        # Keep month/day names and title-like words, discard identifiers/commands.
        if value not in EN_EXACT and value not in IT_EXACT: return False
    return True


def replace_words(text: str, words: dict[str, str]) -> str:
    def repl(match: re.Match[str]) -> str:
        token = match.group(0)
        replacement = words.get(token)
        if replacement is None: replacement = words.get(token.lower())
        if replacement is None: return token
        if token[:1].isupper() and replacement: replacement = replacement[:1].upper() + replacement[1:]
        return replacement
    return re.sub(r"[A-Za-zÁÉÍÓÚÜÑÇáéíóúüñç]+", repl, text)


def translate(text: str, lang: str) -> str:
    if lang == "ES": return text
    exact = EN_EXACT if lang == "EN" else IT_EXACT
    if text in exact: return exact[text]
    out = text
    for old, new in (EN_PHRASES if lang == "EN" else IT_PHRASES): out = out.replace(old, new)
    out = replace_words(out, EN_WORDS if lang == "EN" else IT_WORDS)
    return out


def escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")


def collect() -> list[str]:
    values, seen = [], set()
    if MANIFEST.exists():
        for line in MANIFEST.read_text(encoding="utf-8", errors="ignore").splitlines():
            if not line or line.startswith(";") or "\t" not in line: continue
            _key, encoded = line.split("\t", 1)
            try: value = json.loads(encoded)
            except Exception: continue
            if value not in seen: seen.add(value); values.append(value)
    files = []
    for pattern in SOURCE_GLOBS: files.extend(ROOT.glob(pattern))
    files.extend(ROOT / p for p in SOURCE_FILES)
    for path in sorted(set(files)):
        if not path.exists(): continue
        for value in c_strings(path.read_text(encoding="utf-8", errors="ignore")):
            if visible_candidate(value) and value not in seen:
                seen.add(value); values.append(value)
    return values


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    values = collect()
    MANIFEST.write_text("; hash\tJSON source text\n" + "\n".join(
        f"H{fnv1a(value):08X}\t{json.dumps(value, ensure_ascii=False)}"
        for value in sorted(values, key=lambda item: (fnv1a(item), item))
    ) + "\n", encoding="utf-8")
    names = {"ES":"ESPANOL.LNG", "EN":"ENGLISH.LNG", "IT":"ITALIANO.LNG"}
    for lang, filename in names.items():
        lines = [
            "; BlesKernOS language catalog", f"; code={lang}",
            "; Named keys are for new code; Hxxxxxxxx keys translate legacy programs.", "",
        ]
        for key, value in sorted(NAMED[lang].items()): lines.append(f"{key}={escape(value)}")
        lines.append("")
        for source in sorted(values, key=lambda item: (fnv1a(item), item)):
            lines.append(f"H{fnv1a(source):08X}={escape(translate(source, lang))}")
        lines.append("")
        (OUT / filename).write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(values)} legacy entries in each language pack")

if __name__ == "__main__": main()
