//Perini Lorenzo
#define SERVO1_PIN 9
#define SERVO2_PIN 10
#define PULSANTE_PIN 2
#define LED_STATO_PIN 13
#define SENSORI 5
uint16_t hum[5];    // Umidità per ciascuna delle 5 zone
uint16_t speed[5];  // Velocità per ciascuna zona
bool irrigareZona[5] = {false};
const uint16_t SOGLIA_MIN = 200;  
const uint16_t SOGLIA_MAX = 800;
int oraCorrente = 0;
int minutoCorrente = 0;
int giorno, mese, anno;
bool pulsantePremuto = false;
unsigned long ultimoCambioStato = 0;
const unsigned long debounceDelay = 50;


struct Orario {
  int ora;
  int minuto;
};

#define MAX_ORARI 16
Orario orari[MAX_ORARI];
bool orarioAttivato[MAX_ORARI];
int numOrari = 0;

unsigned long tempoRef = 0;
int ore = 0;
int minuti = 0;
int secondi = 0;

void setupPWM() {
  DDRB |= (1 << PB1) | (1 << PB2); // pin 9 e 10 in output

  // Fast PWM, TOP=ICR1, Non-inverting mode su OC1A/OC1B
  TCCR1A = (1 << WGM11) | (1 << COM1A1) | (1 << COM1B1);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11); // prescaler 8
  ICR1 = 40000; // 20ms periodo → 50Hz

  OCR1A = 1000; // 0°
  OCR1B = 2900; // 90°
}

void setupADC() {
  ADMUX = (1 << REFS0);  // AVcc come riferimento di tensione, canale 0 di default
  ADCSRA = (1 << ADEN)    // Abilita ADC
         | (1 << ADPS2)   // Prescaler 64 → ADC Clock = 16MHz / 64 = 250kHz
         | (1 << ADPS1);
}

uint16_t leggiADC(uint8_t ch) {
  ch &= 0x07; // usa solo i primi 3 bit (massimo 0–7)
  ADMUX = (ADMUX & 0xF0) | ch;      // seleziona un canale da 0 a 5
  ADCSRA |= (1 << ADSC);            // avvia conversione in adc
  while (ADCSRA & (1 << ADSC));     // attende fine conversione
  return ADC;                       // restituisce valore a 10 bit (0–1023)
}

void leggiUmidita(uint16_t hum[5]){
  for (int i = 0; i < 5; i++) {
    hum[i] = leggiADC(i);  // potenziometri A0–A4
  } 
}

void calcolaVelocita(const uint16_t hum[5], uint16_t speed[5]){
  for (int i = 0; i < 5; i++) {
    speed[i] = map(hum[i], 0, 1023, 50, 300);;  // più secco == più lento
  }
}

void angoloServo(uint8_t pin, uint8_t angle){
  uint16_t ticks = map(angle, 0, 180, 1000, 4800);  // Converto angolo in tick per il movimento dei servo

  if (pin == 9) {
    OCR1A = ticks;  // OC1A == pin 9
  } else if (pin == 10) {
    OCR1B = ticks;  // OC1B == pin 10
  }
}

bool troppoSecco(uint16_t valore) {
  return valore < SOGLIA_MIN;
}

bool troppoBagnato(uint16_t valore) {
  return valore > SOGLIA_MAX;
}

void muoviServo(uint8_t pin, const uint16_t speed[5], const uint16_t hum[5]) {
  uint8_t startZona, endZona;

  if (pin == 9) {
    startZona = 0;
    endZona = 2;
  } else {
    startZona = 3;
    endZona = 4;
  }

  for (uint8_t zona = startZona; zona <= endZona; zona++) {

    uint8_t angStart = zona * 36;
    uint8_t angEnd;

    if (zona == 2) {
      angEnd = 90;
    } else if (zona == 4) {
      angEnd = 180;
    } else {
      angEnd = angStart + 35;
    }

    if (troppoSecco(hum[zona]) && !troppoBagnato(hum[zona])) {
      Serial.print("Zona ");
      Serial.print(zona + 1);
      Serial.println(" irrigata.");

      for (uint8_t ang = angStart; ang <= angEnd; ang += 2) {
        angoloServo(pin, ang);
        delay(speed[zona]);
      }

    } else {
      Serial.print("Zona ");
      Serial.print(zona + 1);
      Serial.println(" umida, muovo il servo verso la prossima zona.");

      for (uint8_t ang = angStart; ang <= angEnd; ang += 2) {
        angoloServo(pin, ang);
        delay(5);  
      }
    }
  }
}

void resetServo(){
  OCR1A = 1000; // 0°
  OCR1B = 2900; // 90°
}

void avviaIrrigazione(){
  muoviServo(9,speed, hum);    // Servo 1
  muoviServo(10, speed, hum); // Servo 2
  Serial.println("\n== Irrigazione terminata.\n");
  delay(2000);
  resetServo();
}

void setupPulsante(){
  DDRD &= ~(1 << PD2);    // imposta PD2 (pin 2) come input
  PORTD |= (1 << PD2);    // attiva la modalità pull-up 
}


void setup() {
  setupPWM(); //inizializzo timer1 e i due servo in posizione corretta
  setupADC(); //inizializzo le porte analogiche per accedere ai dati provenienti dai potenziometri
  setupPulsante();

  Serial.begin(9600);

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

  ore = oraCorrente;
  minuti = minutoCorrente;

  Serial.println("== Quanti orari di irrigazione vuoi impostare? (max 16): ");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  numOrari = Serial.readStringUntil('\n').toInt();
  numOrari = constrain(numOrari, 0, MAX_ORARI);

  for (int i = 0; i < numOrari; i++) {
    Serial.print("Orario ");
    Serial.print(i + 1);
    Serial.print(" (hh:mm): \n");
    while (Serial.available() == 0) {}
    String riga = Serial.readStringUntil('\n');
    orari[i].ora = riga.substring(0, 2).toInt();
    orari[i].minuto = riga.substring(3, 5).toInt();
  }

  Serial.println("== Sistema pronto. Modalità sleep attiva.\n");

}



void loop() {
  
  bool lettura = !(PIND & (1 << PD2));  // pulsante premuto → LOW

  if (millis() - tempoRef >= 1000) {    //tengo l'orario aggiornato tramite il timer0 di arduino
    tempoRef = millis();  // aggiorna il tempo di riferimento
    secondi++;            // aumenta i secondi

    //GESTIONE OVERFLOW SECONDI,MINUTI,ORE

    if (secondi == 60) {
      secondi = 0;
      minuti++;

      //Debug orario
      Serial.print("Orario: ");
      if (ore < 10) Serial.print("0");
      Serial.print(ore);
      Serial.print(":");
      if (minuti < 10) Serial.print("0");
      Serial.print(minuti);
    }

    if (minuti == 60) {
      minuti = 0;
      ore++;
    }

    if (ore == 24) {
      ore = 0;
    }

  }

  for(int i=0;i<numOrari;i++){
    if(orari[i].ora==ore && orari[i].minuto==minuti){   //controlliamo anche s per far si che nello stesso minuto non venga controllato piu volte l'orario
        if (!orarioAttivato[i]){
          leggiUmidita(hum);
          calcolaVelocita(hum,speed);
          avviaIrrigazione();
          orarioAttivato[i] = true; // Segna che è gia stato avviato un ciclo in quell'orario
        }
        
    }else{
          orarioAttivato[i] = false;
    }
  }


  if (lettura && !pulsantePremuto && millis() - ultimoCambioStato > debounceDelay) {
    pulsantePremuto = true;
    ultimoCambioStato = millis();

    Serial.println("[MANUALE] Pulsante premuto → Avvio irrigazione manuale...");
    leggiUmidita(hum);
    calcolaVelocita(hum, speed);
    avviaIrrigazione();
    Serial.println("[MANUALE] Irrigazione completata.");
  }

  if (!lettura && pulsantePremuto) {
    pulsantePremuto = false;
    ultimoCambioStato = millis();
  }
}