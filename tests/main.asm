; tests/main.asm
; Punto de entrada principal

EXTERN funcion_externa

_start:
    ; Preparamos algunos registros
    MOV EAX, 10
    MOV EBX, 20
    
    ; Llamamos a la subrutina externa
    CALL funcion_externa
    
    ; Salida del sistema (sys_exit)
    MOV EAX, 1
    MOV EBX, 0
    INT 128