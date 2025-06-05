// developed by Perini Lorenzo
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define SERVO1_PIN 9
#define SERVO2_PIN 10
#define PULSANTE_PIN 2
#define LED_STATO_PIN 13
#define SENSORI 5

int sensori[SENSORI] = {A0, A1, A2, A3, A4};
int soglia_min = 300;
int soglia_max = 700;

bool irrigazione_in_corso = false;
bool sleepMode = false;
bool statoPulsantePrecedente = LOW;
bool wdtWakeUp = false;

volatile uint8_t secondi = 0;
volatile uint32_t tickMillis = 0;
volatile uint32_t tickLED = 0;

int oraCorrente = 0;
int minutoCorrente = 0;
int giorno = 1, mese = 1, anno = 2025;

struct Orario {
  int ora;
  int minuto;
  int durataMinuti;
};

#define MAX_ORARI 16
Orario orari[MAX_ORARI];
int numOrari = 0;

void initADC();
uint16_t readADC(uint8_t canale);
void accendiADC() { ADCSRA |= (1 << ADEN); }
void spegniADC()  { ADCSRA &= ~(1 << ADEN); }

void setupPWM_Timer1() {
  TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
  ICR1 = 40000;
  DDRB |= (1 << PB1) | (1 << PB2);
}

void spegniTimer1() {
  TCCR1A = 0;
  TCCR1B = 0;
}

void impostaAngoloServo(uint8_t canale, uint8_t angolo) {
  uint16_t impulso = map(angolo, 0, 180, 544, 2400);
  uint16_t duty = impulso * 2;
  if (canale == 1)
    OCR1A = duty;
  else if (canale == 2)
    OCR1B = duty;
}

void attesaNonBloccante(uint16_t durata_ms) {
  uint32_t inizio = tickMillis;
  while (tickMillis - inizio < durata_ms);
}

void muoviServo(uint8_t canale, int inizio, int fine, int velocita) {
  setupPWM_Timer1();
  int step = (inizio < fine) ? 1 : -1;
  for (int angolo = inizio; angolo != fine + step; angolo += step) {
    impostaAngoloServo(canale, angolo);
    attesaNonBloccante(velocita);
  }
  spegniTimer1();
}

int leggiUmiditaMedia() {
  accendiADC();
  int somma = 0;
  for (int i = 0; i < SENSORI; i++) {
    somma += readADC(i);
  }
  spegniADC();
  return somma / SENSORI;
}

void eseguiIrrigazioneDurata(int durataMinuti) {
  irrigazione_in_corso = true;
  sleepMode = false;
  uint32_t startSecondi = tickMillis / 1000;
  uint32_t durata = durataMinuti * 60;

  Serial.print("[");
  Serial.print(oraCorrente);
  Serial.print(":");
  Serial.print(minutoCorrente);
  Serial.print("] Irrigazione per ");
  Serial.print(durataMinuti);
  Serial.println(" minuti.");

  while ((tickMillis / 1000) - startSecondi < durata) {
    int media = leggiUmiditaMedia();
    int velocita = map(media, soglia_min, soglia_max, 30, 5);
    velocita = constrain(velocita, 5, 30);

    muoviServo(1, 0, 90, velocita);
    muoviServo(2, 90, 180, velocita);
    attesaNonBloccante(500);
    muoviServo(2, 180, 90, velocita);
    muoviServo(1, 90, 0, velocita);
    attesaNonBloccante(500);
  }

  Serial.println(">>> Fine irrigazione automatica.\n");
  irrigazione_in_corso = false;
  sleepMode = true;
}

int trovaOrarioCorrente() {
  for (int i = 0; i < numOrari; i++) {
    if (orari[i].ora == oraCorrente && orari[i].minuto == minutoCorrente) {
      return i;
    }
  }
  return -1;
}

void eseguiIrrigazioneSingoloCiclo() {
  int media = leggiUmiditaMedia();
  Serial.print("Umidità rilevata: ");
  Serial.println(media);

  if (media > soglia_min) {
    Serial.println("Umidità nella norma. Vuoi irrigare comunque? (S/N): ");
    while (Serial.available() == 0) {}
    char risposta = toupper(Serial.read());
    if (risposta != 'S') {
      Serial.println("Irrigazione annullata.\n");
      return;
    }
  }

  irrigazione_in_corso = true;
  sleepMode = false;

  int velocita = map(media, soglia_min, soglia_max, 30, 5);
  velocita = constrain(velocita, 5, 30);

  Serial.println("Avvio ciclo manuale...\n");
  muoviServo(1, 0, 90, velocita);
  muoviServo(2, 90, 180, velocita);
  attesaNonBloccante(500);
  muoviServo(2, 180, 90, velocita);
  muoviServo(1, 90, 0, velocita);
  attesaNonBloccante(500);

  Serial.println(">>> Fine ciclo manuale.\n");
  irrigazione_in_corso = false;
  sleepMode = true;
}

void aggiornaStatoLED() {
  static bool statoLED = false;

  if (irrigazione_in_corso) {
    if ((tickMillis - tickLED) >= 250) {
      tickLED = tickMillis;
      statoLED = !statoLED;
      if (statoLED)
        PORTB |= (1 << PB5);
      else
        PORTB &= ~(1 << PB5);
    }
  } else if (!sleepMode) {
    int media = leggiUmiditaMedia();
    if (media < soglia_min)
      PORTB |= (1 << PB5);
    else
      PORTB &= ~(1 << PB5);
  } else {
    PORTB &= ~(1 << PB5);
  }
}

void setupWatchdog() {
  MCUSR &= ~(1 << WDRF);
  WDTCSR = (1 << WDCE) | (1 << WDE);
  WDTCSR = (1 << WDIE) | (1 << WDP3); // intervallo 8s
}

void setupInterruptPulsante() {
  EICRA |= (1 << ISC01);
  EIMSK |= (1 << INT0);
}

void entraInSleepMode() {
  // Disabilita periferiche inutili per il risparmio energetico
  PRR |= (1 << PRADC) | (1 << PRTIM1) | (1 << PRUSART0);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();

  // Riabilita le periferiche dopo il risveglio
  PRR &= ~((1 << PRADC) | (1 << PRTIM1) | (1 << PRUSART0));
}

ISR(WDT_vect) {
  wdtWakeUp = true;
}

ISR(TIMER2_COMPA_vect) {
  static uint16_t contatoreMillis = 0;
  contatoreMillis++;
  tickMillis++;

  if (contatoreMillis >= 1000) {
    contatoreMillis = 0;
    secondi++;
    if (secondi >= 60) {
      secondi = 0;
      minutoCorrente++;
      if (minutoCorrente >= 60) {
        minutoCorrente = 0;
        oraCorrente++;
        if (oraCorrente >= 24) {
          oraCorrente = 0;
        }
      }
    }
  }
}

void setupTimer2() {
  TCCR2A = (1 << WGM21);
  TCCR2B = (1 << CS22);
  OCR2A = 249;
  TIMSK2 |= (1 << OCIE2A);
}

void setup() {
  setupPWM_Timer1();
  DDRD &= ~(1 << PD2);
  DDRB |= (1 << PB5);
  PORTB &= ~(1 << PB5);

  for (int i = 0; i < SENSORI; i++) {
    pinMode(sensori[i], INPUT);
  }

  initADC();
  Serial.begin(9600);
  setupTimer2();
  setupWatchdog();
  setupInterruptPulsante();
  sei();

  impostaAngoloServo(1, 0);
  impostaAngoloServo(2, 90);
  attesaNonBloccante(500);
  spegniTimer1();

  Serial.println("== Imposta data corrente (gg/mm/aaaa):");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  String dataStr = Serial.readStringUntil('\n');
  giorno = dataStr.substring(0, 2).toInt();
  mese = dataStr.substring(3, 5).toInt();
  anno = dataStr.substring(6).toInt();

  Serial.println("== Imposta orario corrente (hh:mm):");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  String orarioStr = Serial.readStringUntil('\n');
  oraCorrente = orarioStr.substring(0, 2).toInt();
  minutoCorrente = orarioStr.substring(3, 5).toInt();

  Serial.println("== Quanti orari di irrigazione vuoi impostare? (max 16): ");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  numOrari = Serial.readStringUntil('\n').toInt();
  numOrari = constrain(numOrari, 0, MAX_ORARI);

  for (int i = 0; i < numOrari; i++) {
    Serial.print("Orario ");
    Serial.print(i + 1);
    Serial.print(" (hh:mm durataMinuti): ");
    while (Serial.available() == 0) {}
    String riga = Serial.readStringUntil('\n');
    orari[i].ora = riga.substring(0, 2).toInt();
    orari[i].minuto = riga.substring(3, 5).toInt();
    orari[i].durataMinuti = riga.substring(6).toInt();
  }

  Serial.println("== Sistema pronto. Modalità sleep attiva.\n");
  sleepMode = true;
}

void loop() {
  bool statoCorrente = (PIND & (1 << PD2));

  if (sleepMode && statoCorrente == HIGH && statoPulsantePrecedente == LOW && !irrigazione_in_corso) {
    Serial.println(">>> Pulsante premuto → Avvio irrigazione manuale");
    eseguiIrrigazioneSingoloCiclo();
  }

  static int ultimoMinutoEseguito = -1;
  int indice = trovaOrarioCorrente();
  if (sleepMode && !irrigazione_in_corso && indice != -1 && minutoCorrente != ultimoMinutoEseguito) {
    ultimoMinutoEseguito = minutoCorrente;
    eseguiIrrigazioneDurata(orari[indice].durataMinuti);
  }

  statoPulsantePrecedente = statoCorrente;
  aggiornaStatoLED();

  if (sleepMode && !irrigazione_in_corso) {
    wdtWakeUp = false;
    entraInSleepMode(); // Entra in Power-down
    while (!wdtWakeUp); // Attende risveglio via WDT
  }
}



void initADC() {
  ADMUX = (1 << REFS0);
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);
}

uint16_t readADC(uint8_t canale) {
  ADMUX = (ADMUX & 0xF0) | (canale & 0x0F);
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC));
  return ADC;
}
