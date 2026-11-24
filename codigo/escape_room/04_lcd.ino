/* telas do LCD 16x2 */

void mostrar_tempo(void)
{
    uint8_t minutos  = tempo_restante_s / 60;   /* divisão inteira: minutos */
    uint8_t segundos = tempo_restante_s % 60;   /* resto: segundos */

    lcd_1.setCursor(0, 1);                      /* linha de baixo */
    if (minutos < 10)  lcd_1.print('0');
    lcd_1.print(minutos);
    lcd_1.print(':');
    if (segundos < 10) lcd_1.print('0');
    lcd_1.print(segundos);
}

void mostrar_letra(void)
{
    lcd_1.setCursor(7, 1);
    lcd_1.print("LETRA:");
    if (fase1_digitada) lcd_1.print(fase1_digitada);
    else                lcd_1.print('_');
}

void mostrar_erros(void)
{
    if (estado == JOGO_FASE1) lcd_1.setCursor(14, 0);
    else                      lcd_1.setCursor(15, 1);
    lcd_1.print(erros);
}

void lcd_linha(uint8_t linha, const char *texto)    /* reescreve a linha toda, sem clear() (clear pisca a tela) */
{
    lcd_1.setCursor(0, linha);
    uint8_t i = 0;
    while (texto[i] != '\0' && i < 16) {
        lcd_1.print(texto[i]);
        i++;
    }
    while (i < 16) {
        lcd_1.print(' ');
        i++;
    }
}

void lcd_linha0(const char *texto)
{
    lcd_linha(0, texto);
}

void mostrar_linha1_tempo_erros(void)   /* "mm:ss    ERROS:n" (fase 2 e senha) */
{
    mostrar_tempo();
    lcd_1.setCursor(5, 1);
    lcd_1.print("    ERROS:");
    lcd_1.print(erros);
}

void mostrar_linha0_fase1(void)
{
    char texto[17] = "FASE 1  ERROS:";
    texto[14] = '0' + erros;
    texto[15] = '\0';
    lcd_linha0(texto);
}
