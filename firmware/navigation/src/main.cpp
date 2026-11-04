#include <Arduino.h>
#include <Servo.h>
#include <Metro.h>

// --- Configuration des Pins ---
const int speedPin_M1 = 5;
const int speedPin_M2 = 6;
const int directionPin_M1 = 4;
const int directionPin_M2 = 7;
const int URPWM = 3;
const int URTRIG = 10;
const int servoPin = 9;

// --- Seuils adaptés au labyrinthe ---
const int OBSTACLE_THRESHOLD = 25;  // Distance de détection frontale réduite pour labyrinthe
const int SAFE_TURN_DISTANCE = 20;  // Distance minimale des murs latéraux (réduite pour labyrinthe)
const int WALL_AVOID_DISTANCE = 15; // Distance critique des murs - déclenche correction
const int SCAN_INTERVAL = 800;      // Intervalle entre scans pendant l'avancement (ms)

// --- Objets et Variables ---
Servo myservo;
Metro measureTimer = Metro(50);
Metro scanTimer = Metro(SCAN_INTERVAL); // Timer pour surveillance périodique
int actualDistance = 1000;
uint8_t EnPwmCmd[4] = {0x44, 0x02, 0xbb, 0x01};

int getStableDistance(int numMeasurements = 3);
int getDistance();
void decidePath();
void monitorWalls();
void extendedScan(int& extremeRight, int& extremeLeft);
void carStop();
void carAdvance(int leftSpeed, int rightSpeed);
void carBack(int leftSpeed, int rightSpeed);
void carTurnLeft(int leftSpeed, int rightSpeed);
void carTurnRight(int leftSpeed, int rightSpeed);

void setup() {
  // Initialisation série d'abord
  Serial.begin(9600);
  delay(100);

  // Initialisation des pins moteurs
  pinMode(directionPin_M1, OUTPUT);
  pinMode(directionPin_M2, OUTPUT);
  pinMode(speedPin_M1, OUTPUT);
  pinMode(speedPin_M2, OUTPUT);

  // Arrêt complet des moteurs au démarrage
  carStop();
  delay(500);

  // Initialisation du capteur ultrasonique
  pinMode(URTRIG, OUTPUT);
  digitalWrite(URTRIG, HIGH);
  pinMode(URPWM, INPUT);

  // Initialisation du servo
  myservo.attach(servoPin);
  delay(100);
  myservo.write(90);
  delay(500);

  // Commande d'initialisation du driver moteur (si nécessaire)
  for (int i = 0; i < 4; i++) {
    Serial.write(EnPwmCmd[i]);
    delay(10);
  }

  Serial.println("Robot initialized - Ready to go!");

  // Test rapide des moteurs pour vérifier le fonctionnement
  Serial.println("Testing motors...");
  carAdvance(100, 100);
  delay(500);
  carStop();
  delay(200);
  carBack(100, 100);
  delay(500);
  carStop();
  delay(200);

  Serial.println("Motor test complete - Starting main program!");
  delay(1000);
}

void loop() {
  if (measureTimer.check()) {
    actualDistance = getStableDistance(2); // 2 mesures rapides pour surveillance continue
    Serial.println(actualDistance); // Log de la distance pour debug
  }

  if (actualDistance > 0 && actualDistance <= OBSTACLE_THRESHOLD) {
    Serial.println("Obstacle detected!");
    carStop();
    delay(300); // Délai plus long pour stabilité
    decidePath();
    actualDistance = 1000; // Reset pour éviter rebouclage immédiat
    scanTimer.reset(); // Reset du timer de surveillance
  } else {
    // Avancer normalement
    carAdvance(120, 120);

    // Surveiller les murs pendant l'avancement (toutes les SCAN_INTERVAL ms)
    monitorWalls();
  }
}

// --- Scan étendu pour détecter les sorties aux extrêmes ---
void extendedScan(int& extremeRight, int& extremeLeft) {
  Serial.println("Extended scan - checking extreme sides...");

  // Scan extrême droite (0° - complètement à droite)
  myservo.write(0);
  delay(400);
  extremeRight = getStableDistance(3);
  Serial.print("Extreme Right (0°): "); Serial.print(extremeRight); Serial.println("cm");

  // Scan extrême gauche (180° - complètement à gauche)
  myservo.write(180);
  delay(400);
  extremeLeft = getStableDistance(3);
  Serial.print("Extreme Left (180°): "); Serial.print(extremeLeft); Serial.println("cm");

  // Retour au centre
  myservo.write(90);
  delay(300);
}

// --- Mesure améliorée avec filtrage ---
int getStableDistance(int numMeasurements) {
  int validMeasurements[10]; // Stockage temporaire des mesures valides
  int validCount = 0;

  for (int i = 0; i < numMeasurements; i++) {
    unsigned long duration = pulseIn(URPWM, LOW, 30000);
    int distance = (duration == 0 || duration >= 50000) ? 1000 : duration / 50;

    // Filtrer les valeurs absurdes (> 1000 ou == 1000)
    if (distance < 1000 && distance > 0) {
      validMeasurements[validCount++] = distance;
    }
    delay(50); // Petit délai entre mesures pour stabilité
  }

  // Si aucune mesure valide, retourner erreur
  if (validCount == 0) return 1000;

  // Calculer la moyenne des mesures valides
  int sum = 0;
  for (int i = 0; i < validCount; i++) {
    sum += validMeasurements[i];
  }
  return sum / validCount;
}

// --- Mesure simple (conservée pour compatibilité) ---
int getDistance() {
  unsigned long duration = pulseIn(URPWM, LOW, 30000);
  if (duration == 0 || duration >= 50000) return 1000;
  return duration / 50;
}

// --- Surveillance des murs pendant l'avancement ---
void monitorWalls() {
  if (!scanTimer.check()) return; // Pas encore temps de scanner

  Serial.println("Monitoring walls...");

  // Scan rapide des côtés (sans centre pour économie de temps)
  int distRight, distLeft;

  // Scan droite rapide
  myservo.write(45); // Angle plus fermé pour rapidité
  delay(200);
  distRight = getStableDistance(2); // 2 mesures seulement pour rapidité

  // Scan gauche rapide
  myservo.write(135); // Angle plus fermé
  delay(200);
  distLeft = getStableDistance(2);

  // Retour rapide au centre
  myservo.write(90);
  delay(150);

  Serial.print("Walls - Right:"); Serial.print(distRight);
  Serial.print("cm Left:"); Serial.print(distLeft); Serial.println("cm");

  // Correction si trop proche des murs
  if (distRight < WALL_AVOID_DISTANCE || distLeft < WALL_AVOID_DISTANCE) {
    Serial.println("TOO CLOSE TO WALLS - CORRECTING COURSE");

    carStop();
    delay(100);

    // Calculer correction basée sur les distances
    if (distRight < WALL_AVOID_DISTANCE && distLeft >= WALL_AVOID_DISTANCE) {
      // Trop proche du mur droit, tourner légèrement à gauche
      Serial.println("Correcting LEFT (right wall too close)");
      carTurnLeft(180, 150); // Tourner plus lentement pour précision
      delay(300);
    }
    else if (distLeft < WALL_AVOID_DISTANCE && distRight >= WALL_AVOID_DISTANCE) {
      // Trop proche du mur gauche, tourner légèrement à droite
      Serial.println("Correcting RIGHT (left wall too close)");
      carTurnRight(150, 180);
      delay(300);
    }
    else if (distRight < WALL_AVOID_DISTANCE && distLeft < WALL_AVOID_DISTANCE) {
      // Les deux murs trop proches - demi-tour
      Serial.println("Both walls too close - U-TURN");
      carBack(200, 200);
      delay(400);
      carTurnRight(255, 255);
      delay(800);
    }

    // Reset timer pour éviter corrections trop fréquentes
    scanTimer.reset();
  }
}

// --- Décision de chemin pour labyrinthe ---
void decidePath() {
  int distRight, distCenter, distLeft;

  Serial.println("Obstacle detected - scanning paths...");

  // Scan complet pour décision d'itinéraire
  myservo.write(45);  // Angle plus fermé pour labyrinthe
  delay(300);
  distRight = getStableDistance(3);

  myservo.write(90);
  delay(250);
  distCenter = getStableDistance(3);

  myservo.write(135); // Angle plus fermé
  delay(300);
  distLeft = getStableDistance(3);

  myservo.write(90);
  delay(200);

  Serial.print("Paths - Right:"); Serial.print(distRight);
  Serial.print("cm Center:"); Serial.print(distCenter);
  Serial.print("cm Left:"); Serial.print(distLeft); Serial.println("cm");

  // Vérifier si les chemins sont trop étroits - scan étendu nécessaire
  bool needsExtendedScan = (distRight < SAFE_TURN_DISTANCE && distLeft < SAFE_TURN_DISTANCE) ||
                          (distRight < SAFE_TURN_DISTANCE && distCenter < OBSTACLE_THRESHOLD) ||
                          (distLeft < SAFE_TURN_DISTANCE && distCenter < OBSTACLE_THRESHOLD);

  int extremeRight = 1000, extremeLeft = 1000;

  if (needsExtendedScan) {
    Serial.println("Paths too narrow - performing extended scan for exits...");
    extendedScan(extremeRight, extremeLeft);

    // Vérifier si les extrêmes offrent de meilleures options
    if (extremeRight > SAFE_TURN_DISTANCE || extremeLeft > SAFE_TURN_DISTANCE) {
      Serial.println("Found potential exit at extreme positions!");
      distRight = max(distRight, extremeRight);
      distLeft = max(distLeft, extremeLeft);
    }
  }

  // Pour labyrinthe : privilégier les chemins dégagés
  bool rightClear  = (distRight  > SAFE_TURN_DISTANCE);
  bool leftClear   = (distLeft   > SAFE_TURN_DISTANCE);
  bool centerClear = (distCenter > OBSTACLE_THRESHOLD + 5); // +5cm marge

  Serial.print("Decision - Right:"); Serial.print(rightClear ? "CLEAR" : "BLOCKED");
  Serial.print(" Center:"); Serial.print(centerClear ? "CLEAR" : "BLOCKED");
  Serial.print(" Left:"); Serial.println(leftClear ? "CLEAR" : "BLOCKED");

  // Stratégie pour labyrinthe : éviter les demi-tours autant que possible
  if (centerClear) {
    // Centre dégagé - continuer tout droit avec surveillance
    Serial.println("Center clear - continuing forward with wall monitoring");
    // Pas de mouvement spécial, continuer avec monitorWalls()
  }
  else if (rightClear && leftClear) {
    // Les deux côtés dégagés - choisir le plus large
    Serial.println("Both sides clear - choosing wider path");
    if (distRight >= distLeft) {
      carBack(100, 100); delay(150); // Recul plus court et lent
      carTurnRight(120, 120); delay(500); // Virage plus lent
      Serial.println("Turning RIGHT");
    } else {
      carBack(100, 100); delay(150);
      carTurnLeft(120, 120); delay(500); // Virage plus lent
      Serial.println("Turning LEFT");
    }
  }
  else if (rightClear && !leftClear) {
    Serial.println("Only right clear - turning RIGHT");
    carBack(100, 100); delay(150);
    carTurnRight(120, 120); delay(500);
  }
  else if (leftClear && !rightClear) {
    Serial.println("Only left clear - turning LEFT");
    carBack(100, 100); delay(150);
    carTurnLeft(120, 120); delay(500);
  }
  else {
    // Impasse complète - demi-tour
    Serial.println("Dead end - U-TURN required");
    carBack(150, 150); delay(300);
    carTurnRight(180, 180); delay(600);
  }
}

// --- Fonctions de Mouvement avec logs ---
void carStop() {
  analogWrite(speedPin_M1, 0);
  analogWrite(speedPin_M2, 0);
  Serial.println("carStop");
}

void carAdvance(int leftSpeed, int rightSpeed) {
  analogWrite(speedPin_M2, leftSpeed);
  digitalWrite(directionPin_M1, LOW);   // LOW pour avancer
  analogWrite(speedPin_M1, rightSpeed);
  digitalWrite(directionPin_M2, LOW);   // LOW pour avancer
  Serial.println("carAdvance");
}

void carBack(int leftSpeed, int rightSpeed) {
  analogWrite(speedPin_M2, leftSpeed);
  digitalWrite(directionPin_M1, HIGH);  // HIGH pour reculer
  analogWrite(speedPin_M1, rightSpeed);
  digitalWrite(directionPin_M2, HIGH);  // HIGH pour reculer
  Serial.println("carBack");
}

void carTurnLeft(int leftSpeed, int rightSpeed) {
  analogWrite(speedPin_M2, leftSpeed);
  digitalWrite(directionPin_M1, LOW);
  analogWrite(speedPin_M1, rightSpeed);
  digitalWrite(directionPin_M2, HIGH);
  Serial.println("carTurnLeft");
}

void carTurnRight(int leftSpeed, int rightSpeed) {
  analogWrite(speedPin_M2, leftSpeed);
  digitalWrite(directionPin_M1, HIGH);
  analogWrite(speedPin_M1, rightSpeed);
  digitalWrite(directionPin_M2, LOW);
  Serial.println("carTurnRight");
}

