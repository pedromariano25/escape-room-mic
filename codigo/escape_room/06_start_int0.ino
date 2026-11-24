/* botão START no INT0 (D2) com debounce */

void start_atualizar(void)
{
    if (!BIT_TST(START_PIN, START_BIT)) {       /* pino em 0 = botão apertado (pull-up) */
        start_solto_ms = 0;                         /* apertado ou repicando */
    } else if (start_solto_ms < SOLTO_MIN_MS) {
        start_solto_ms++;
    }

    if (!start_pronto && start_solto_ms >= SOLTO_MIN_MS) {
        start_pronto = 1;                           /* ficou 50 ms solto */
        flag_start = 0;                             /* bordas guardadas eram repique */
    }
}

uint8_t start_apertado(void)
{
    if (flag_start) {
        flag_start = 0;
        if (start_pronto) {
            start_pronto = 0;
            return 1;
        }
    }
    return 0;
}
