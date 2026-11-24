/* teclado 3x4 pelo PCF8574 no I2C por software (D5/D6) */

void pcf_escrever(uint8_t valor)
{
    i2c_teclado.beginTransmission(ENDERECO_TECLADO);
    i2c_teclado.write(valor);
    i2c_teclado.endTransmission();
}

uint8_t pcf_ler(void)
{
    i2c_teclado.requestFrom((uint8_t)ENDERECO_TECLADO, (uint8_t)1);
    if (i2c_teclado.available()) return i2c_teclado.read();
    return 0xFF;
}

void teclado_selecionar_linha(uint8_t linha)
{
    pcf_escrever(0xFF & ~(1 << BIT_LINHA[linha])); /* só a linha escolhida em 0, o resto em 1 */
}

int8_t teclado_coluna_apertada(void)
{
    uint8_t leitura = pcf_ler();
    for (uint8_t coluna = 0; coluna < 3; coluna++) {
        if (!BIT_TST(leitura, BIT_COLUNA[coluna])) return coluna; /* coluna em 0 = tecla apertada */
    }
    return -1;
}

void teclado_atualizar(void)            /* uma linha a cada 5 ms, tecla vale depois de 2 varreduras iguais */
{
    teclado_div++;
    if (teclado_div < TECLADO_PERIODO_MS) return;
    teclado_div = 0;

    int8_t coluna = teclado_coluna_apertada();
    if (coluna >= 0) teclado_tecla_varredura = TECLAS[teclado_linha][coluna];

    teclado_linha++;
    if (teclado_linha >= 4) {
        teclado_linha = 0;

        if (teclado_tecla_varredura == teclado_candidata) {
            if (teclado_contagem < 255) teclado_contagem++;
        } else {
            teclado_candidata = teclado_tecla_varredura;
            teclado_contagem = 0;
        }

        if (teclado_contagem >= TECLADO_ESTAVEL && teclado_candidata != teclado_reportada) {
            teclado_reportada = teclado_candidata;
            if (teclado_candidata != 0) teclado_evento = teclado_candidata;
        }

        teclado_tecla_varredura = 0;
    }

    teclado_selecionar_linha(teclado_linha);    /* prepara a próxima linha */
}

char teclado_ler(void)
{
    char tecla = teclado_evento;
    teclado_evento = 0;
    return tecla;
}
