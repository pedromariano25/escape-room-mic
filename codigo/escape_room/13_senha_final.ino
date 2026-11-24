/* SENHA FINAL: digitar as 4 letras na ordem */

void senha_desenhar(void)
{
    char texto[17] = "SENHA: ";
    for (uint8_t i = 0; i < SENHA_TAM; i++) {
        texto[7 + i] = (i < senha_tam) ? senha_digitada[i] : '_';
    }
    texto[7 + SENHA_TAM] = '\0';
    lcd_linha0(texto);
}

void senha_limpar(void)
{
    senha_tam = 0;
    senha_digitada[0] = '\0';
}

void senha_entrar(void)
{
    estado = JOGO_SENHA;
    senha_limpar();
    lcd_linha(1, "");
    mostrar_linha1_tempo_erros();
    senha_desenhar();
    uart_enviar_linha("EVENTO SENHA");
}

void senha_tratar_tecla(char tecla)
{
    if (tecla >= 'A' && tecla <= 'Z') {
        if (senha_tam < SENHA_TAM) {
            senha_digitada[senha_tam++] = tecla;
            senha_digitada[senha_tam] = '\0';
        }
        senha_desenhar();
    } else if (tecla == '#') {
        if (senha_tam > 0) {                    /* # apaga a última letra */
            senha_tam--;
            senha_digitada[senha_tam] = '\0';
        }
        senha_desenhar();
    } else if (tecla == '*' && senha_tam > 0) { /* * confirma */
        if (senha_tam == SENHA_TAM && strcmp(senha_digitada, SENHA_CERTA) == 0) { /* 4 letras e igual a AVAI */
            vitoria();                          /* desarmou a bomba */
        } else {
            senha_limpar();
            registrar_erro();                   /* mostra "ERRO!" e depois volta pra senha */
        }
    }
}
