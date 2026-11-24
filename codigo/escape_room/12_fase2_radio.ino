/* FASE 2 (Vinícius): o rádio, gira o pot até o buzzer calar e descobre a letra pela frequência */

uint16_t distancia(uint16_t a, uint16_t b)
{
    if (a > b) return a - b;
    return b - a;
}

void fase2_comecar_rodada(void)
{
    f2_estado = F2_PROCURANDO;
    f2_alvo_definido = 0;
    f2_na_zona = 0;
    f2_letra = 0;
    f2_som_div = 0;
    f2_tela_mudou = 1;
}

void fase2_sortear_alvo(void)
{
    uint16_t alvo = ALVO_MIN;
    for (uint8_t tentativa = 0; tentativa < 20; tentativa++) {
        alvo = ALVO_MIN + (uint16_t)(rand() % (ALVO_MAX - ALVO_MIN + 1)); /* sorteia o ponto de silêncio */
        if (distancia(alvo, f2_pot) >= DIST_MIN_SORTEIO) break;
    }
    f2_alvo = alvo;
    f2_alvo_definido = 1;
}

/* o período cresce junto com a distância, então perto do alvo o tom fica bem agudo */
uint16_t fase2_freq_quente_frio(uint16_t dist)
{
    if (dist > 1023) dist = 1023;
    uint32_t periodo_us = PERIODO_PERTO_US
                        + (((uint32_t)(PERIODO_LONGE_US - PERIODO_PERTO_US) * dist) >> 10); /* >> 10 = dividir por 1024 */
    return (uint16_t)(1000000UL / periodo_us);  /* período -> frequência */
}

uint8_t fase2_usando_buzzer(void)
{
    return estado == JOGO_FASE2 && f2_estado == F2_PROCURANDO;
}

void fase2_desenhar(void)
{
    char texto[17];

    if (f2_estado == F2_PROCURANDO) {
        strcpy(texto, "RADIO 1/2: GIRE");
        texto[6] = '1' + f2_rodada;
    } else {
        char numero[6];
        utoa(F2_FREQ_RODADA[f2_rodada], numero, 10);
        strcpy(texto, numero);
        strcat(texto, "Hz LETRA:");
        uint8_t n = strlen(texto);
        texto[n] = f2_letra ? f2_letra : '_';
        texto[n + 1] = '\0';
    }
    lcd_linha0(texto);
}

void fase2_entrar(void)
{
    srand(TCNT1 ^ (ler_ms_no_segundo() << 6) ^ (tempo_restante_s << 3)); /* semente aleatória: depende de quando o jogador chegou aqui */

    estado = JOGO_FASE2;
    fase1_leds_apagar();

    for (uint8_t i = 0; i < MEDIA_N; i++) f2_amostras[i] = 0;
    f2_soma = 0;
    f2_indice = 0;
    f2_qtd_amostras = 0;
    f2_adc_div = 0;
    f2_rodada = 0;
    fase2_comecar_rodada();

    mostrar_linha1_tempo_erros();
    fase2_desenhar();
    f2_tela_mudou = 0;
}

void fase2_ler_potenciometro(void)
{
    uint16_t valor;
    if (adc_ler_resultado(&valor)) {            /* média móvel das 8 últimas leituras */
        f2_soma -= f2_amostras[f2_indice];      /* tira a leitura mais velha */
        f2_amostras[f2_indice] = valor;
        f2_soma += valor;                       /* soma a nova */
        f2_indice = (f2_indice + 1) & (MEDIA_N - 1); /* & 7: volta pro 0 depois do 7 */
        if (f2_qtd_amostras < MEDIA_N) f2_qtd_amostras++;
        if (f2_qtd_amostras == MEDIA_N) f2_pot = f2_soma >> MEDIA_SHIFT; /* >> 3 = dividir por 8 */
    }

    f2_adc_div++;
    if (f2_adc_div >= ADC_PERIODO_MS && !adc_ocupado()) {
        f2_adc_div = 0;
        adc_iniciar_conversao(ADC_CANAL_POT);   /* pede outra leitura do pot */
    }
}

void fase2_atualizar(void)              /* chamada a cada 1 ms */
{
    fase2_ler_potenciometro();

    if (f2_qtd_amostras < MEDIA_N) return;
    if (!f2_alvo_definido) fase2_sortear_alvo();

    uint16_t d = distancia(f2_pot, f2_alvo);    /* quão longe do alvo */
    if (f2_na_zona) {                           /* histerese: entra a 40, sai a 55 */
        if (d > ZONA_SAI) f2_na_zona = 0;
    } else {
        if (d <= ZONA_ENTRA) f2_na_zona = 1;
    }

    if (f2_estado == F2_PROCURANDO) {
        f2_som_div++;
        if (f2_som_div >= SOM_PERIODO_MS) {
            f2_som_div = 0;
            if (melodia_tocando()) return;      /* melodia tem prioridade */
            if (f2_na_zona) buzzer_desligar();  /* no alvo: silêncio */
            else            buzzer_tocar(fase2_freq_quente_frio(d)); /* fora: tom mais agudo quanto mais perto */
        }
    }
}

void fase2_start(void)
{
    if (f2_estado != F2_PROCURANDO || !f2_alvo_definido) return;

    if (f2_na_zona) {
        buzzer_desligar();
        f2_estado = F2_DIGITANDO;               /* achou: mostra a frequência e espera a letra */
        f2_letra = 0;
        f2_tela_mudou = 1;
    } else {
        registrar_erro();                       /* START fora da zona é erro */
    }
}

void fase2_tratar_tecla(char tecla)
{
    if (f2_estado != F2_DIGITANDO) return;

    if (tecla >= 'A' && tecla <= 'Z') {
        f2_letra = tecla;
        f2_tela_mudou = 1;
    } else if (tecla == '#') {
        f2_letra = 0;
        f2_tela_mudou = 1;
    } else if (tecla == '*' && f2_letra != 0) {
        if (f2_letra == F2_LETRA_CERTA[f2_rodada]) {
            f2_rodada++;
            if (f2_rodada >= F2_RODADAS) senha_entrar(); /* acabaram as 2 rodadas: senha final */
            else                         fase2_comecar_rodada();
        } else {
            registrar_erro();                   /* errou: -30 s */
            if (estado == JOGO_DERROTA) return;
            fase2_comecar_rodada();             /* repete a rodada com outro alvo */
        }
    }
}
