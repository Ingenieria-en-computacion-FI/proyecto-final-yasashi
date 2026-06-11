; tests/utils.asm
; Modulo de funciones auxiliares

funcion_externa:
    ; Simplemente sumamos los registros preparados en main
    ADD EAX, EBX
    
    ; Retornamos al flujo principal
    RET