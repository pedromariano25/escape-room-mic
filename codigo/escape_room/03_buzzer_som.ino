/* buzzer no Timer2 (CTC invertendo o D11) e as melodias de erro, bônus, vitória e boom */

void buzzer_desligar(void)
{
    TCCR2B = 0;                                 /* para o Timer2 */
    TCCR2A = (1 << WGM21);                      /* COM2A0 = 0: o timer solta o D11 */
    BIT_CLR(BUZZER_PORT, BUZZER_BIT);           /* deixa o D11 em 0 */
    buzzer_ligado = 0;
}

/* f = 16 MHz / (2 * N * (OCR2A + 1)), testa os prescalers do menor pro maior até o OCR2A caber em 8 bits */
void buzzer_tocar(uint16_t freq_hz)
{
    if (freq_hz == 0) {
        buzzer_desligar();
        return;
    }
    for (uint8_t i = 0; i < 7; i++) {
        uint32_t divisor = 2UL * PRESCALERS_T2[i] * freq_hz; /* 2 * N * f */
        uint32_t passos  = (F_CPU + divisor / 2) / divisor; /* OCR2A + 1, já arredondado */

        if (passos >= 1 && passos <= 256) {     /* cabe nos 8 bits do Timer2 */
            uint8_t ocr = (uint8_t)(passos - 1);
            uint8_t cs  = i + 1;                /* CS22:20 de 1 a 7 escolhe o prescaler */

            if (buzzer_ligado && ocr == buzzer_ocr && cs == buzzer_cs) return; /* já tá tocando essa nota */

            OCR2A  = ocr;                       /* define a frequência */
            TCCR2A = (1 << COM2A0) | (1 << WGM21); /* CTC + inverte o D11 a cada volta */
            if (TCNT2 > ocr) TCNT2 = 0;         /* se já passou do novo OCR, recomeça */
            TCCR2B = cs;                        /* prescaler: o som começa */

            buzzer_ligado = 1;
            buzzer_ocr = ocr;
            buzzer_cs = cs;
            return;
        }
    }
    buzzer_desligar();
}

/* ---------- melodias (erro, bonus, vitoria, boom) ---------- */

void melodia_tocar(const Nota *melodia, uint8_t tamanho)
{
    melodia_atual = melodia;
    melodia_tamanho = tamanho;
    melodia_indice = 0;
    bip_tocando = 0;
    buzzer_tocar(melodia[0].freq_hz);
    melodia_ms = melodia[0].duracao_ms;
}

uint8_t melodia_tocando(void)
{
    return melodia_atual != 0;
}

void melodia_atualizar(void)            /* chamada a cada 1 ms */
{
    if (!melodia_atual) return;

    if (melodia_ms > 0) melodia_ms--;
    if (melodia_ms > 0) return;

    melodia_indice++;
    if (melodia_indice >= melodia_tamanho) {
        melodia_atual = 0;
        buzzer_desligar();
        return;
    }
    buzzer_tocar(melodia_atual[melodia_indice].freq_hz);
    melodia_ms = melodia_atual[melodia_indice].duracao_ms;
}
