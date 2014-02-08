;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                                 ;;
;; Copyright (C) KolibriOS team 2004-2014. All rights reserved.    ;;
;; Distributed under terms of the GNU General Public License       ;;
;;                                                                 ;;
;;   Written by hidnplayr@kolibrios.org                            ;;
;;                                                                 ;;
;;         GNU GENERAL PUBLIC LICENSE                              ;;
;;          Version 2, June 1991                                   ;;
;;                                                                 ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


socket_connect:

; ignore if status is not "disconnected"
        cmp     [status], STATUS_DISCONNECTED
        jne     .reconnect

        if TIMESTAMP
        call    print_timestamp
        end if
        mov     esi, str_connecting
        call    print_text2
        mov     esi, irc_server_name
        call    print_text2
        mov     esi, str_dotnewline
        call    print_text2

; update status
        inc     [status]        ; was STATUS_DISCONNECTED, now STATUS_RESOLVING

; resolve name
        push    esp     ; reserve stack place
        push    esp     ; fourth parameter
        push    0       ; third parameter
        push    0       ; second parameter
        push    irc_server_name
        call    [getaddrinfo]
        pop     esi
; test for error
        test    eax, eax
        jnz     .fail_dns

; fill in ip in sockstruct
        mov     eax, [esi + addrinfo.ai_addr]
        mov     eax, [eax + sockaddr_in.sin_addr]
        mov     [sockaddr1.ip], eax

; free allocated memory
        push    esi
        call    [freeaddrinfo]

; update status
        inc     [status]

; connect
        mcall   socket, AF_INET4, SOCK_STREAM, 0
        cmp     eax, -1
        jz      .fail
        mov     [socketnum], eax

        mcall   connect, [socketnum], sockaddr1, 18
        cmp     eax, -1
        jz      .fail_refused

        ret

  .fail:
        mov     [status], STATUS_DISCONNECTED

        if TIMESTAMP
        call    print_timestamp
        end if
        mov     esi, str_sockerr
        call    print_text2

        ret

  .fail_dns:
        mov     [status], STATUS_DISCONNECTED

        if TIMESTAMP
        call    print_timestamp
        end if
        mov     esi, str_dnserr
        call    print_text2

        ret

  .fail_refused:
        mov     [status], STATUS_DISCONNECTED

        if TIMESTAMP
        call    print_timestamp
        end if
        mov     esi, str_refused
        call    print_text2

        ret

  .reconnect:
        if TIMESTAMP
        call    print_timestamp
        end if
        mov     esi, str_reconnect
        call    print_text2

        mov     esi, quit_msg
        call    cmd_usr_quit.with_message

        jmp     socket_connect


socket_write_userinfo:

; create packet in packetbuf
        mov     edi, packetbuf

        mov     eax, 'NICK'
        stosd
        mov     al, ' '
        stosb
        mov     esi, user_nick
        mov     ecx, MAX_NICK_LEN
  .loop:
        lodsb
        test    al, al
        jz      .done
        stosb
        dec     ecx
        jnz     .loop
  .done:
        mov     ax, 0x0a0d
        stosw

        mov     eax, 'USER'
        stosd
        mov     al, ' '
        stosb
        mov     esi, user_nick
        mov     ecx, MAX_NICK_LEN
  .loop2:
        lodsb
        test    al, al
        jz      .done2
        stosb
        dec     ecx
        jnz     .loop2
  .done2:
        mov     eax, ' 8 *'
        stosd
        mov     ax, ' :'
        stosw
        mov     al, ' '
        stosb
        mov     esi, user_real_name
        mov     ecx, MAX_REAL_LEN
  .loop3:
        lodsb
        test    al, al
        jz      .done3
        stosb
        dec     ecx
        jnz     .loop3
  .done3:
        mov     ax, 0x0a0d
        stosw

        lea     esi, [edi - packetbuf]
        mcall   send, [socketnum], packetbuf, , 0

        ret




process_network_event:
; values for status: 0, 1, 2, 3
        mov     eax, [status]
        dec     eax
; 0 = STATUS_DISCONNECTED - do nothing
; (ignore network events if we are disconnected from network)
        js      .nothing
; 1 = STATUS_RESOLVING
        jz      .nothing
; 2 = STATUS_CONNECTING
        dec     eax
        jz      .connecting
; 3 = STATUS_CONNECTED
        jmp     .connected

  .nothing:
        ret

  .connecting:
        call    socket_write_userinfo

; The connection has been established, change status from "connecting" to "connected".
        inc     [status]

  .connected:
        call    socket_receive
        ret


socket_receive:

        pusha

; TODO: read more data if we receive one full packet

  .nextpacket:
        mcall   recv, [socketnum], packetbuf, 1024, MSG_DONTWAIT        ; read a packet
        inc     eax                                                     ; check if we got any data
        jz      .done                                                   ; TODO: handle errors!
        dec     eax
        jz      .disconnected

; ok we have data, now feed it to the recoder

        lea     edx, [packetbuf + eax]                  ; edx = end pointer
        mov     esi, packetbuf                          ; esi = start pointer
  .nextcommand:
        mov     edi, servercommand
  .byteloop:
        call    get_next_byte                           ; reads byte from [esi] to al
        jnc     .nextpacket                             ; if CF is set, we need more data (TODO: dont throw away old data!!!)
        cmp     al, 10
        je      .got_command
        cmp     al, 13
        je      .got_command
        stosb
        jmp     .byteloop

; we have a command, call the serverparser

  .got_command:
        mov     byte[edi], 0                            ; mark the end of the command
        push    esi edx
        call    server_parser
        pop     edx esi
        jmp     .nextcommand

  .done:
        popa
        ret


  .disconnected:
        mov     esi, str_disconnected
        call    print_text2

        mov     [status], STATUS_DISCONNECTED
        mcall   close, [socketnum]

        popa
        ret