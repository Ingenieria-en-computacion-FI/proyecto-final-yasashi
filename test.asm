; =====================================================================
; UNIVERSIDAD NACIONAL AUTÓNOMA DE MÉXICO
; Estructura y Programación de Computadoras - Trabajo Final
; Archivo de Prueba de Cobertura Adaptado (Base 10)
; =====================================================================

inicio:
    ; 1. Casos mínimos: MOV Inmediato y registro a registro
    MOV EAX, 10                 
    MOV EBX, EAX                

    ; 2. Instrucciones Unarias y Aritméticas
    INC EAX                     
    ADD EBX, 5                  
    SUB EBX, EAX                
    CMP EBX, 20                 

    ; 3. Referencia adelantada crítica
    JE procesar_sib             

ciclo_espera:
    ; 4. Saltos hacia atrás y bucles
    DEC ECX                     
    JNE ciclo_espera            
    JMP fin                     

procesar_sib:
    ; 5. SIB COMPLEJO (Base + Índice * Escala + Desplazamiento)
    MOV EDX, [ EBX + ECX * 4 + 8 ] 
    
    ; 6. Operaciones Lógicas y Transferencia a Pila
    AND EDX, 65535              ; Convertido a Decimal (0x0000FFFF = 65535)
    PUSH EDX                    
    CALL rutina_interna         
    POP EAX                     

rutina_interna:
    ; 7. Direccionamiento con desplazamiento base
    MOV ECX, [ EBP + 4 ]          
    MOV EAX, 1                  ; Sustitución temporal de RET si no está soportado en tu Lexer

fin:
    ; 8. Cierre del programa
    XOR EAX, EAX