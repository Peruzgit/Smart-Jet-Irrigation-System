//developed by Perini Lorenzo
// === Definizione dei PIN e delle costanti del sistema ===
#define SERVO1_PIN 9              // Pin per controllo del primo servo
#define SERVO2_PIN 10             // Pin per controllo del secondo servo
#define PULSANTE_PIN 2            // Pin collegato al pulsante per attivazione manuale
#define LED_STATO_PIN 13          // Pin del LED di stato (esterno o integrato)
#define SENSORI 5                 // Numero di sensori di umidità

// Array dei pin analogici collegati ai sensori
int sensori[SENSORI] = {A0, A1, A2, A3, A4};

// Soglie per valutare l'umidità del terreno
int soglia_min = 300;
int soglia_max = 700;

// Variabili di stato
bool irrigazione_in_corso = false;
bool sleepMode = false;
bool statoPulsantePrecedente = LOW;

// Tempo di riferimento per l'orologio interno
unsigned long tempoBase;

// Variabili per data e ora
int oraCorrente = 0;
int minutoCorrente = 0;
int giorno = 1, mese = 1, anno = 2025;

// Struttura per memorizzare gli orari di irrigazione
struct Orario {
  int ora;
  int minuto;
  int durataMinuti;
};

#define MAX_ORARI 16              // Numero massimo di orari memorizzabili
Orario orari[MAX_ORARI];         // Array di orari
int numOrari = 0;                // Numero effettivo di orari impostati

// === Funzione per scrivere un segnale PWM per i servomotori ===
void scriviPWM(int pin, int angolo) {
  int pulse = map(angolo, 0, 180, 544, 2400); // Converte angolo in microsecondi
  digitalWrite(pin, HIGH);
  delayMicroseconds(pulse);
  digitalWrite(pin, LOW);
  delay(20 - pulse / 1000); // Attende fine del periodo da 20ms
}

// === Movimento fluido dei servomotori tra due angoli con velocità controllata ===
void muoviServo(int pin, int inizio, int fine, int velocita) {
  int step = (inizio < fine) ? 1 : -1;
  for (int angolo = inizio; angolo != fine + step; angolo += step) {
    scriviPWM(pin, angolo);
    delay(velocita);
  }
}

// === Calcola la media delle letture dei sensori di umidità ===
int leggiUmiditaMedia() {
  int somma = 0;
  for (int i = 0; i < SENSORI; i++) {
    somma += analogRead(sensori[i]);
  }
  return somma / SENSORI;
}

// === Avvia l'irrigazione automatica per una durata specifica ===
void eseguiIrrigazioneDurata(int durataMinuti) {
  irrigazione_in_corso = true;
  sleepMode = false;
  unsigned long inizio = millis();
  unsigned long durata = durataMinuti * 60000UL;

  // Log su monitor seriale
  Serial.print("[");
  Serial.print(oraCorrente);
  Serial.print(":");
  Serial.print(minutoCorrente);
  Serial.print("] Irrigazione per ");
  Serial.print(durataMinuti);
  Serial.println(" minuti.");

  // Ciclo di irrigazione
  while (millis() - inizio < durata) {
    int media = leggiUmiditaMedia();
    int velocita = map(media, soglia_min, soglia_max, 5, 30);
    velocita = constrain(velocita, 5, 30);

    // Movimento dei servomotori
    muoviServo(SERVO1_PIN, 0, 90, velocita);
    muoviServo(SERVO2_PIN, 90, 180, velocita);
    delay(500);
    muoviServo(SERVO2_PIN, 180, 90, velocita);
    muoviServo(SERVO1_PIN, 90, 0, velocita);
    delay(500);
  }

  Serial.println(">>> Fine irrigazione automatica.\n");
  irrigazione_in_corso = false;
  sleepMode = true;
}

// === Aggiorna l'orario interno basato sul tempo trascorso ===
void aggiornaOrario() {
  unsigned long elapsed = (millis() - tempoBase) / 1000;
  int minutiTotali = (oraCorrente * 60 + minutoCorrente + elapsed / 60) % (24 * 60);
  oraCorrente = minutiTotali / 60;
  minutoCorrente = minutiTotali % 60;
}

// === Cerca se l'orario attuale corrisponde ad un orario di irrigazione ===
int trovaOrarioCorrente() {
  for (int i = 0; i < numOrari; i++) {
    if (orari[i].ora == oraCorrente && orari[i].minuto == minutoCorrente) {
      return i;
    }
  }
  return -1;
}

// === Avvio manuale dell'irrigazione su richiesta dell'utente ===
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

  // Avvio ciclo manuale
  irrigazione_in_corso = true;
  sleepMode = false;

  int velocita = map(media, soglia_min, soglia_max, 5, 30);
  velocita = constrain(velocita, 5, 30);

  Serial.println("Avvio ciclo manuale...\n");
  muoviServo(SERVO1_PIN, 0, 90, velocita);
  muoviServo(SERVO2_PIN, 90, 180, velocita);
  delay(500);
  muoviServo(SERVO2_PIN, 180, 90, velocita);
  muoviServo(SERVO1_PIN, 90, 0, velocita);
  delay(500);

  Serial.println(">>> Fine ciclo manuale.\n");
  irrigazione_in_corso = false;
  sleepMode = true;
}

// === Gestione dello stato del LED in base alle condizioni ===
void aggiornaStatoLED() {
  static unsigned long ultimoBlink = 0;
  static bool statoLED = false;

  if (irrigazione_in_corso) {
    // Lampeggio rapido durante irrigazione (2Hz)
    if (millis() - ultimoBlink >= 250) {
      ultimoBlink = millis();
      statoLED = !statoLED;
      digitalWrite(LED_STATO_PIN, statoLED);
    }
  } else if (!sleepMode) {
    // LED acceso se troppo secco, spento altrimenti
    int media = leggiUmiditaMedia();
    digitalWrite(LED_STATO_PIN, media < soglia_min ? HIGH : LOW);
  } else {
    // Sleep mode: LED spento
    digitalWrite(LED_STATO_PIN, LOW);
  }
}

// === Funzione di inizializzazione del sistema ===
void setup() {
  // Configura i pin
  pinMode(SERVO1_PIN, OUTPUT);
  pinMode(SERVO2_PIN, OUTPUT);
  pinMode(PULSANTE_PIN, INPUT);
  pinMode(LED_STATO_PIN, OUTPUT);
  digitalWrite(LED_STATO_PIN, LOW);

  // Inizializza i pin dei sensori
  for (int i = 0; i < SENSORI; i++) {
    pinMode(sensori[i], INPUT);
  }

  // Avvia comunicazione seriale
  Serial.begin(9600);

  // Inizializza posizione dei servomotori
  scriviPWM(SERVO1_PIN, 0);
  scriviPWM(SERVO2_PIN, 90);
  delay(500);

  // Acquisizione data
  Serial.println("== Imposta data corrente (gg/mm/aaaa):");
  while (Serial.available()) Serial.read(); // Pulisce buffer seriale
  while (Serial.available() == 0) {}
  String dataStr = Serial.readStringUntil('\n');
  giorno = dataStr.substring(0, 2).toInt();
  mese = dataStr.substring(3, 5).toInt();
  anno = dataStr.substring(6).toInt();

  // Acquisizione orario
  Serial.println("== Imposta orario corrente (hh:mm):");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  String orarioStr = Serial.readStringUntil('\n');
  oraCorrente = orarioStr.substring(0, 2).toInt();
  minutoCorrente = orarioStr.substring(3, 5).toInt();
  tempoBase = millis(); // Salva il tempo base per l’orologio

  // Acquisizione orari irrigazione
  Serial.println("== Quanti orari di irrigazione vuoi impostare? (max 16): ");
  while (Serial.available()) Serial.read();
  while (Serial.available() == 0) {}
  numOrari = Serial.readStringUntil('\n').toInt();
  numOrari = constrain(numOrari, 0, MAX_ORARI);

  // Inserimento orari
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

// === Funzione principale che viene eseguita ciclicamente ===
void loop() {
  aggiornaOrario(); // Aggiorna orologio interno

  // Lettura stato pulsante
  bool statoCorrente = digitalRead(PULSANTE_PIN);

  // Avvio irrigazione manuale con pulsante
  if (sleepMode && statoCorrente == HIGH && statoPulsantePrecedente == LOW && !irrigazione_in_corso) {
    Serial.println(">>> Pulsante premuto → Avvio irrigazione manuale");
    eseguiIrrigazioneSingoloCiclo();
  }

  // Controlla se è il momento di avviare irrigazione programmata
  static int ultimoMinutoEseguito = -1;
  int indice = trovaOrarioCorrente();
  if (sleepMode && !irrigazione_in_corso && indice != -1 && minutoCorrente != ultimoMinutoEseguito) {
    ultimoMinutoEseguito = minutoCorrente;
    eseguiIrrigazioneDurata(orari[indice].durataMinuti);
  }

  // Aggiorna stato pulsante per rilevare nuovi press
  statoPulsantePrecedente = statoCorrente;

  // Aggiorna LED di stato in base al contesto
  aggiornaStatoLED();
}
