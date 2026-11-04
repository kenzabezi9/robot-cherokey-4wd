#include <Arduino.h>
#include <Wire.h>
#include "HUSKYLENS.h"

// --- Configuration des Pins Moteurs ---
const int speedPin_M1 = 5;
const int speedPin_M2 = 6;
const int directionPin_M1 = 4;
const int directionPin_M2 = 7;

// --- Vitesses ---
const int BASE_SPEED = 120;
const int TURN_SPEED = 100;

HUSKYLENS huskylens;
//HUSKYLENS green line >> SDA; blue line >> SCL
void printResult(HUSKYLENSResult result);

// --- Fonctions de Contrôle des Moteurs ---
void carStop() {
  digitalWrite(directionPin_M1, HIGH);
  digitalWrite(directionPin_M2, HIGH);
  analogWrite(speedPin_M1, 0);
  analogWrite(speedPin_M2, 0);
}

void carAdvance(int leftSpeed, int rightSpeed) {
  digitalWrite(directionPin_M1, LOW);
  digitalWrite(directionPin_M2, LOW);
  analogWrite(speedPin_M1, leftSpeed);
  analogWrite(speedPin_M2, rightSpeed);
}

void carTurnLeft(int leftSpeed, int rightSpeed) {
  digitalWrite(directionPin_M1, LOW);
  digitalWrite(directionPin_M2, HIGH);
  analogWrite(speedPin_M1, leftSpeed);
  analogWrite(speedPin_M2, rightSpeed);
}

void carTurnRight(int leftSpeed, int rightSpeed) {
  digitalWrite(directionPin_M1, HIGH);
  digitalWrite(directionPin_M2, LOW);
  analogWrite(speedPin_M1, leftSpeed);
  analogWrite(speedPin_M2, rightSpeed);
}

void setup() {
    Serial.begin(115200);
    
    // Initialisation des pins moteurs
    pinMode(directionPin_M1, OUTPUT);
    pinMode(directionPin_M2, OUTPUT);
    pinMode(speedPin_M1, OUTPUT);
    pinMode(speedPin_M2, OUTPUT);
    carStop();
    
    Wire.begin();
    while (!huskylens.begin(Wire))
    {
        Serial.println(F("Begin failed!"));
        Serial.println(F("1.Please recheck the \"Protocol Type\" in HUSKYLENS (General Settings>>Protocol Type>>I2C)"));
        Serial.println(F("2.Please recheck the connection."));
        delay(100);
    }
    Serial.println(F("HuskyLens OK!"));
}

void loop() {
    if (!huskylens.request()) Serial.println(F("Fail to request data from HUSKYLENS, recheck the connection!"));
    else if(!huskylens.isLearned()) {
        Serial.println(F("Nothing learned, press learn button on HUSKYLENS to learn one!"));
        carStop();
    }
    else if(!huskylens.available()) {
        Serial.println(F("No block or arrow appears on the screen!"));
        carStop();
    }
    else
    {
        Serial.println(F("###########"));
        while (huskylens.available())
        {
            HUSKYLENSResult result = huskylens.read();
            printResult(result);
            
            // Contrôle des moteurs basé sur le bloc de couleur
            if (result.command == COMMAND_RETURN_BLOCK) {
                int xC = result.xCenter;
                int yC = result.yCenter;
                int width = result.width;
                int centerX = 160;
                int tolerance = 30;
                int minWidth = 50;  // Distance minimale (objet loin)
                int maxWidth = 150; // Distance maximale (objet proche)
                
                // Gestion de la distance basée sur la largeur du bloc
                if (width < minWidth) {
                    // Objet trop loin - avancer plus vite
                    carAdvance(BASE_SPEED + 20, BASE_SPEED + 20);
                    Serial.println(F("Avancer plus vite (loin)"));
                }
                else if (width > maxWidth) {
                    // Objet trop proche - s'arrêter
                    carStop();
                    Serial.println(F("Arreter (proche)"));
                }
                else {
                    // Distance normale - suivre la direction
                    if (xC < centerX - tolerance) {
                        // Bloc à gauche - tourner à gauche
                        carTurnLeft(TURN_SPEED, BASE_SPEED);
                        Serial.println(F("Tourner a gauche"));
                    }
                    else if (xC > centerX + tolerance) {
                        // Bloc à droite - tourner à droite
                        carTurnRight(BASE_SPEED, TURN_SPEED);
                        Serial.println(F("Tourner a droite"));
                    }
                    else {
                        // Bloc au centre - avancer tout droit
                        carAdvance(BASE_SPEED, BASE_SPEED);
                        Serial.println(F("Avancer tout droit"));
                    }
                }
            }
        }    
    }
    delay(100);
}

void printResult(HUSKYLENSResult result){
    if (result.command == COMMAND_RETURN_BLOCK){
        Serial.println(String()+F("Block:xCenter=")+result.xCenter+F(",yCenter=")+result.yCenter+F(",width=")+result.width+F(",height=")+result.height+F(",ID=")+result.ID);
    }
    else if (result.command == COMMAND_RETURN_ARROW){
        Serial.println(String()+F("Arrow:xOrigin=")+result.xOrigin+F(",yOrigin=")+result.yOrigin+F(",xTarget=")+result.xTarget+F(",yTarget=")+result.yTarget+F(",ID=")+result.ID);
    }
    else{
        Serial.println("Object unknown!");
    }
}