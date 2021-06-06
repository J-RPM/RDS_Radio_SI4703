/*        *** Receptor de radio FM+RDS con ARDUINO: Radio_SI4.ino ***
               Basado en el código y librerías de Matthias Hertel
                        Módulo de radio: SI4703
                          
          IMPORTANTE, las siguientes librerías están modificadas:
                         radio.h - radio.cpp 
                        SI4703.h - SI4703.cpp
                     RDSParser.h - RDSParser.cpp
____________________________________________________________________________________
                         Escrito por: J_RPM
                          http://j-rpm.com
                        v1.2 >>> Abril de 2021
    *** RT-RDS en el LCD, con caracteres latinos de la tabla G0 ***
____________________________________________________________________________________

         Original author Matthias Hertel, http://www.mathertel.de
             copyright Copyright (c) 2014 by Matthias Hertel
             This work is licensed under a BSD style license
                 See http://www.mathertel.de/License.aspx
         brief Radio implementation using the Serial communication.
  
                        Arduino   |  SI4703 
                     :----------: | :-------: 
                            GND   |    GND  
                           3.3V   |    VCC  
                             A5   |    SCLK 
                             A4   |    SDIO 
                             D2   |    RST 
____________________________________________________________________________________
*/
#include <LiquidCrystal.h>
#include <Wire.h>

#include "radio.h"
#include "si4703.h"
#include "RDSParser.h"
#define REINICIAR asm("jmp 0x0000")

// En Setup(), se reserva el espacio máximo de cada String en memoria RAM
const String HW="(v1.2)";   // Versión actual del programa
String inSerie;             // Entrada de datos serie
String bak_RT;              // Máximo: 64 caracteres 
String rds_RT;              // Máximo: 64+15 >>> 79 caracteres
String rds_PS;
String rds_TpTa;
String rds_Pty; 

char gruposRds[10];
char grupo;
char bak_GR; 
uint8_t indexGroup=0;
uint8_t i_sidx=0;         // Inicia el receptor con la presintonía 0
uint8_t i_max=23;         // Index del número de presintonías  
uint8_t bak_Pty=32;
uint8_t cuenta2=150;      // Contador para el refresco PS y Scroll del LCD
uint16_t block1_bak;

bool errorRDS;
bool _inGR=false;
bool InGR=false;
bool modoRDS=false;                 // Marcador del modo PS/RT 
bool inRT = false;                  // Entrada de datos RT-RDS
bool Refresh_LCD = false;           // Marca para el refresco del LCD

//volatile:  todas las variables que se manejan dentro de la interrupción
volatile uint8_t cuenta=0;   
volatile uint8_t punto=0;      
volatile uint8_t cambio=0;

/////////////////////////////////////////////////////////////////////////////////////////////////
// Se sustituye el array de frecuencias 'preset[]' por 'const uint16_t preset(uint8_t nSet)'   //
//  *** Así se ahorran 46 Bytes en la RAM, y pueden ser usados la ejecución del programa ***   // 
/////////////////////////////////////////////////////////////////////////////////////////////////
/*/  23 frecuencias presintonizadas (Madrid Norte)
// Ejemplo: 88.20 MHz >>> 8820 // 104.90 MHz >>> 10490
RADIO_FREQ preset[] = {
  8820, // PI:E211 / PS:RNE 1    (CE: Torrespaña)
  8900, // PI:E230 / PS:LOS40 CL 
  9030, // PI:E215 / PS:RNE 5-M  (CE: Torrespaña)
  9100, // PI:E2ED / PS:EUROPAFM
  9170, // PI:E237 / PS:DIAL
  9240, // PI:E238 / PS:RADIOLE
  9320, // PI:E213 / PS:Radio 3  (CE: Torrespaña)
  9390, // PI:E235 / PS:LOS40 
  9510, // PI:E256 / PS: INTER ··· ECONOMIA
  9580, // PI:E213 / PS:Radio 3  (CE: Navacerrada)
  9650, // PI:E212 / PS:RNE-CLAS (CE: Torrespaña)
  9720, // PI:E2E7 / PS:TOPRADIO
  9800, // PI:E2EE / PS:ONDACERO
  9880, // PI:E212 / PS:RNE-CLAS (CE: Navacerrada)
  9950, // PI:E2CE / PS:CAD-100
 10070, // PI:E2DC / PS:MegaStar
 10130, // PI:EDDD / PS:O.MADRID
 10170, // PI:E2FF / PS:ROCK FM
 10270, // PI:E2EC / PS:KISS FM
 10430, // PI:E236 / PS:SER+
 10490, // PI:E211 / PS:RNE 1    (CE: Navacerrada)
 10540, // PI:E239 / PS:SER
 10630  // PI:E2CA / PS:COPE
};
/*///////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////
// Filtro del RDS por país (MSB hexadecimal del PI: E### = 14 decimal  //
//       SYMBOL FOR PI: http://poupa.cz/rds/countrycodes.htm           // 
/////////////////////////////////////////////////////////////////////////
// 0 = El receptor no filtra por PI (El RDS muestra algunos datos erróneos)
// 1..15 = MSB del código PI, en valor decimal (E = 14) 
// Por ejemplo en España= 14 (Hexadecimal: E)
/////////////////////////////////////////////////////////////////////////
uint8_t countryPY = 0;       // Sin filtro PI
// uint8_t countryPY = 14;   // Filtro PI para: España, Suecia, Etiopía, Zambia...
/////////////////////////////////////////////////////////////////////////

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Lista de poulsadores
enum KEYSTATE {
  KEYSTATE_NONE, KEYSTATE_SELECT, KEYSTATE_LEFT, KEYSTATE_UP, KEYSTATE_DOWN, KEYSTATE_RIGHT
} __attribute__((packed));

// Función para determinar el botón pulsado
KEYSTATE getLCDKeypadKey();

// Chip del receptor de radio utilizado
SI4703 radio;         //< Crea una instancia para el chip SI4703

// Interface para mostrar el RDS
RDSParser rds;

// Definición del estado
enum RADIO_STATE {
  STATE_PARSECOMMAND, //< waiting for a new command character.
  STATE_PARSEINT,     //< waiting for digits for the parameter.
  STATE_EXEC          //< executing the command.
};

RADIO_STATE state;    //< The state variable is used for parsing input characters.

// Define los 8 carácteres CGRAM del LCD
  byte es_A[8]={ //á
    B00010,
    B00100,
    B01110,
    B00001,
    B01111,
    B10001,
    B01111,
    B00000,
  };
  byte es_E[8]={ //é
    B00010,
    B00100,
    B01110,
    B10001,
    B11111,
    B10000,
    B01110,
    B00000,
  };
  byte es_I[8]={ //í
    B00010,
    B00100,
    B01100,
    B00100,
    B00100,
    B00100,
    B01110,
    B00000,
  };
  byte es_O[8]={ //ó
    B00010,
    B00100,
    B01110,
    B10001,
    B10001,
    B10001,
    B01110,
    B00000,
  };
  byte es_U[8]={ //ú
    B00010,
    B00100,
    B10001,
    B10001,
    B10001,
    B10011,
    B01101,
    B00000,
  };
  byte es_N[8]={ //ñ
    B01110,
    B00000,
    B10110,
    B11001,
    B10001,
    B10001,
    B10001,
    B00000,
  };
  byte es_C[8]={ //ç
    B00000,
    B01110,
    B10000,
    B10000,
    B10001,
    B01110,
    B00100,
    B00000,
  };
  byte es_uU[8]={  //ü
    B01010,
    B00000,
    B10001,
    B10001,
    B10001,
    B10011,
    B01101,
    B00000,
  };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Setup a FM only radio configuration with I/O for commands and debugging on the Serial port.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void setup() {
  // Se reserva el espacio máximo de cada String en memoria RAM
  bak_RT.reserve(64);         // Máximo: 64 caracteres 
  rds_RT.reserve(64+15);      // Máximo: 64+15 = 79 caracteres
  rds_PS.reserve(8);          // 8 caracteres
  rds_TpTa.reserve(2);        // 2 caracteres
  rds_Pty.reserve(8);         // 8 caracteres

  
  // Interrupción Timer2, para el refresco del PS/RT y presentaciones en el display LCD
  SREG = (SREG & 0b01111111);               //Desabilitar interrupciones
  TIMSK2 = TIMSK2|0b00000001;               //Habilita la interrupcion por desbordamiento
  TCCR2B = 0b00000111;                      //Configura preescala para que FT2 sea de 7812.5Hz
  SREG = (SREG & 0b01111111) | 0b10000000;  //Habilitar interrupciones 
  
  // Open the Serial port
  Serial.begin(4800);
  Serial.println(F("------------------------------------"));
  Serial.print(F("Inicializando el chip: SI4703 "));
  Serial.println(HW);

  // Inicializa el receptor de radio
  radio.init();

  // Inicializa el LCD 
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print(F("Radio_SI4 "));
  lcd.print(HW);
  lcd.setCursor(0, 1);
  lcd.print(F("... cargando RAM"));
  delay(500);

  // Almacena los caracteres extra en la CGRAM del LCD
  lcd.createChar(0x08, es_A);     //á
  lcd.createChar(0x09, es_E);     //é  
  lcd.createChar(0x0A, es_I);     //í
  lcd.createChar(0x0B, es_O);     //ó
  lcd.createChar(0x0C, es_U);     //ú
  lcd.createChar(0x0D, es_N);     //ñ
  lcd.createChar(0x0E, es_C);     //ç
  lcd.createChar(0x0F, es_uU);    //ü
  delay(500);

  // Presenta los 8 caracteres CGRAM en el LCD
  // CGRAM se encuentra: 0x00...0x07 
  // y también en: 0x08...0x0F
  lcd.setCursor(0, 1);
  lcd.print(F("CGRAM: "));
  for (int i=0x08; i<=0x10; i++){
    lcd.print((char)i);
  }

  // Inicia el receptor con la presintonía seleccionada
  //radio.setBandFrequency(RADIO_BAND_FM, preset[i_sidx]); 
  radio.setBandFrequency(RADIO_BAND_FM, preset(i_sidx)); 
  radio.setMono(false);
  radio.setMute(false);
  radio.setVolume(1);
  serialFreeRam();
  
  delay(3000);
  lcd.clear();

  state = STATE_PARSECOMMAND;
  
  // Setup the information chain for RDS data.
  radio.attachReceiveRDS(RDS_process);
  rds.attachServicenNameCallback(DisplayServiceName);
  rds.attachTimeCallback(DisplayTime);
  rds.attachPiCallback(DisplayPi);
  rds.attachGroupCallback(DisplayGroup);
  rds.attachVariantCallback(DisplayVariant);
  rds.attachPtyCallback(DisplayPty);
  rds.attachTpTaCallback(DisplayTpTa);
  rds.attachTextCallback(DisplayRT);
  
  // Muestra el volumen por el puerto serie
  Serial.print(F("# Volumen: "));
  Serial.println(radio.getVolume());
  borraRDS(true);

} // Setup
////////////////////////////////////////////////////////////////////////////
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// Constantly check for serial input commands and trigger command execution.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
////////////////////////////////////////////////////////////////////////////
void loop() {
  unsigned long now = millis();
  static unsigned long nextFreqTime = 0;
  static RADIO_FREQ lastf = 0;
  RADIO_FREQ f = 0;
  
  // Decodifica los datos RDS, con o sin filtro PI (countryPY = 0 >>> sin filtro)
  radio.checkRDS(countryPY);
 
  // Analiza las pulsaciones, para realizar cambios de sintonía o volumen
  KEYSTATE k = getLCDKeypadKey();

  if (k == KEYSTATE_RIGHT) {
    Serial.println(F("# Scan +"));
    radio.seekUp(true);
    borraRDS(true);

  } else if (k == KEYSTATE_UP) {
    // Sube el volumen
    int v = radio.getVolume();
    if (v < 15) {
      radio.setVolume(++v);
      // Muestra el volumen por el puerto serie
      Serial.print(F("# Volumen: "));
      Serial.println(radio.getVolume());
    }
    borraRDS(false);  // Borra todo lo memorizado, menos el PS
 
  } else if (k == KEYSTATE_DOWN) {
    // Baja el volumen
    int v = radio.getVolume();
    if (v > 0) {
      radio.setVolume(--v);
      // Muestra el volumen por el puerto serie
      Serial.print(F("# Volumen: "));
      Serial.println(v);
    }
    borraRDS(false);  // Borra todo lo memorizado, menos el PS
 
  } else if (k == KEYSTATE_LEFT) {
    Serial.println(F("# Scan -"));
    radio.seekDown(true);
    borraRDS(true);

  } else if (k == KEYSTATE_SELECT) {
    // Cambia de presintonía
    i_sidx++;
    if (i_sidx >= i_max) { i_sidx=0; }
    Serial.print(F("# Presintonía: "));
    Serial.println(i_sidx+1);
    //radio.setFrequency(preset[i_sidx]);
    radio.setFrequency(preset(i_sidx));
    borraRDS(true);
  }

  // Refresca la frecuencia del display cada 0,4 segundos, si existen cambios
  if (now > nextFreqTime) {
    f = radio.getFrequency();
    
    if (f != lastf || errorRDS) {
      // Muestra la frecuencia actual
      DisplayFrequency(f);
      lastf = f;
      errorRDS=false;
      radio.debugRadioInfo();
    }
    nextFreqTime = now + 400;
  } 

  // Refresco en LCD:  Grupos/PTY/TpTa/PS/RT y actividad
  if (Refresh_LCD==true) refrescoLCD();

  // Cambio de frecuencia a través del puerto serie
  while (Serial.available() > 0) {
    int inChar = Serial.read();
    // Convierte el Byte de entrara a CHAR y lo añade al string
    if (isDigit(inChar)) {
      inSerie += (char)inChar;
    }
    
    // RX RS232 de una orden nueva, terminada con un punto
    if (inChar == '.') {
      int freqNew = inSerie.toInt();

      // Presintonías: 1. >>> 23.
      if (freqNew >= 1 && freqNew <= 23){
        Serial.print(F("# Presintonía: "));
        Serial.println(freqNew);
        i_sidx=freqNew-1;
        //radio.setFrequency(preset[i_sidx]);
        radio.setFrequency(preset(i_sidx));
        borraRDS(true);
      // Reiniciar Arduino: 99.
      }else if (freqNew == 99){
        Serial.println(F("<<< REINICIANDO >>>"));
        delay(250);
        REINICIAR;
      // Cambio de frecuencia
      }else if (freqNew >= 8750 && freqNew <= 10800){
        Serial.print(F("# Frecuencia: "));
        Serial.println(freqNew);
        radio.setFrequency(freqNew);
        borraRDS(true);
      }else {
        Serial.print(F("### RX Error >>> "));
        Serial.println(inSerie);
        Serial.print(F("### Presintonía: [1.]-[")); Serial.print(i_max); Serial.println(F(".]"));
        Serial.println(F("### Frecuencia: [8750.]-[10800.]"));
        Serial.println(F("### REINICIAR: [99.]"));
      }
      inSerie = "";
    }
  }
  
} // loop
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// This function determines the current pressed key but
// * doesn't return short key down situations that are key bouncings.
// * returns a specific key down only once.
//////////////////////////////////////////////////////////////////
KEYSTATE getLCDKeypadKey() {
  static unsigned long lastChange = 0;
  static KEYSTATE lastKey = KEYSTATE_NONE;
  unsigned long now = millis();
  
  KEYSTATE newKey;
  // Toma el estado del botón pulsado
  int v = analogRead(A0); // Se lee el botón pulsado midiendo la tensión en A0
  
  if (v < 100) {              // Medida real: 0
    newKey = KEYSTATE_RIGHT;  
  } else if (v < 200) {       // Medida real: 130
    newKey = KEYSTATE_UP;     
  } else if (v < 400) {       // Medida real: 303
    newKey = KEYSTATE_DOWN;   
  } else if (v < 600) {       // Medida real: 476
    newKey = KEYSTATE_LEFT;   
  } else if (v < 800) {       // Medida real: 720
    newKey = KEYSTATE_SELECT;
  } else {
    newKey = KEYSTATE_NONE;
  }

  if (newKey != lastKey) {
    // a new situation - just remember but do not return anything pressed.
    lastChange = now;
    lastKey = newKey;
    return (KEYSTATE_NONE);

  } else if (lastChange == 0) {
    // nothing new.
    return (KEYSTATE_NONE);

  } if (now > lastChange + 100) {
    // now its not a bouncing button any more.
    lastChange = 0; // don't report twice
    return (newKey);

  } else {
    return (KEYSTATE_NONE);

  } // if
} // getLCDKeypadKey()
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// This function will be when a new frequency is received.
// Update the Frequency on the LCD display.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
void DisplayFrequency(RADIO_FREQ f) {
  char s[12];
  radio.formatFrequency(s, sizeof(s));
  String mFreq = String(s);
  mFreq = mFreq.substring(0, 5) + "  ";
  lcd.setCursor(0, 0); 
  lcd.print(mFreq);
  // Muestra la frecuencia por el puerto serie
  Serial.print(F("Frecuencia -> "));
  Serial.println(mFreq);
} 
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// This function will be called by the RDS module when a new ServiceName is available.
// Update the LCD to display the ServiceName in row 1 chars 0 to 7.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
void DisplayServiceName(char *name) {
  rds_PS=name;
  // Muestra el PS por el puerto serie
  Serial.print(F("PS -> "));
  Serial.println(name);
} 
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// This function will be called by the RDS module when a rds time message was received.
// Update the LCD to display the time in right upper corner.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
void DisplayTime(uint8_t hour, uint8_t minute) {
  // Muestra la HORA por el puerto serie
  Serial.print(F("CT -> "));
  lcd.setCursor(0, 0); 
  lcd.print(F(" "));
  if (hour < 10) {
    lcd.print(F("0"));
    Serial.print(F("0"));
  }
  lcd.print(hour);
  lcd.print(F(":"));
  Serial.print(hour);
  Serial.print(F(":"));
  if (minute < 10) {
    lcd.print(F("0"));
    Serial.print(F("0"));
  }
  lcd.print(minute);
  Serial.println(minute);
} 
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void RDS_process(uint16_t block1, uint16_t block2, uint16_t block3, uint16_t block4) {
  rds.processData(block1, block2, block3, block4);
  ///////////////////////////////////////////////////////////////
  // TEST: Muestra toda la información RDS por el puerto serie //
  ///////////////////////////////////////////////////////////(///
  //printHex4(block1);
  //printHex4(block2);
  //printHex4(block3);
  //printHex4(block4);
  //Serial.println ("");
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void DisplayPi(uint16_t rdsPI) {
  // Si cambia el PI, se reinician los datos del RDS
  if (block1_bak != rdsPI){
    block1_bak = rdsPI;
    errorRDS=true;
    borraGrupos();
  }

  // Muestra el PI en el LCD
  lcd.setCursor(9, 1); 
  RDS_LCD(rdsPI);
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void DisplayGroup(uint16_t rdsGroup) {
  // Muestra en el LCD, el Grupo recibido del RDS 
  _inGR=true;   // Marca la actividad del RDS
  intToCharGroup(rdsGroup);
  if (bak_GR!=grupo){
    bak_GR=grupo;
    lcd.setCursor(14, 1); 
    lcd.print(grupo);
    entraGrupo(grupo);
  }    
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void DisplayVariant(uint8_t rdsVariant) {
  // Muestra en el LCD, la variante del Grupo RDS recibido
  lcd.setCursor(15, 1); 
  lcd.print (rdsVariant, HEX);
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void borraRDS(bool flag) {
  errorRDS=true;
  if (flag==true){
    rds.init();
    rds_PS="........";
  }
  lcd.setCursor(5, 0); 
  lcd.print(F("  #--------"));
  lcd.setCursor(0, 1); 
  lcd.print(rds_PS);
  int v = radio.getVolume();
  if (v==0 || v==15){
    lcd.print(F("#"));
  }else{
    lcd.print(F("}"));
  }
  lcd.print(F("     --"));
  borraGrupos();
}
//////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////
void borraGrupos() {
  inRT=false;
  _inGR=false;
  rds_RT="";
  bak_RT="";
  bak_GR="";
  bak_Pty=32;
  rds_Pty="";
  rds_TpTa="";
  modoRDS=false;
  cuenta2=150;
  cambio=0;
  punto=0;

  for (int i=0; i<8; i++) {
    gruposRds[i]='-';    
  }
  gruposRds[8]="\0";
  indexGroup=0;
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void printHex4(uint16_t val) {
  if (val <= 0x000F) Serial.print(F("0"));     
  if (val <= 0x00FF) Serial.print(F("0"));     
  if (val <= 0x0FFF) Serial.print(F("0"));     
  Serial.print(val, HEX);
  Serial.print(F(" "));
} 
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void RDS_LCD(uint16_t val) {
  if (val <= 0x000F) lcd.print(F("0"));     
  if (val <= 0x00FF) lcd.print(F("0"));     
  if (val <= 0x0FFF) lcd.print(F("0"));     
  lcd.print(val, HEX);
  lcd.print(F(" "));
} 
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void entraGrupo(char g) {
  for (int i=0; i<8; i++) {
    if (gruposRds[i] == g) {
      return;
    } else if (i==indexGroup && gruposRds[i] == '-') {
      gruposRds[indexGroup]=g;
      ordenaGrupos();
      indexGroup++;
      return;
    }
  }
}
//////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
void ordenaGrupos() {
  char temporal;

  // Ordena los grupos RDS de menor a mayor
  for (int j=0; j < indexGroup; j++) {
    temporal=gruposRds[j];
    for (int i=0; i < indexGroup; i++) {
      if (gruposRds[i+1] < gruposRds[i] && (gruposRds[i+1] != '-')) {
        temporal=gruposRds[i];
        gruposRds[i]=gruposRds[i+1];
        gruposRds[i+1]=temporal;
      }
    }
  }
}
//////////////////////////////////////////////////////////////////
void DisplayRT(char _rds_RT[64]){
  if (byte(_rds_RT[0]) > 32 && byte(_rds_RT[1]) > 32){
    // El nuevo RT entra convertido, con los caracteres latinos del LCD
    inRT=true;
    bak_RT=String(_rds_RT);
    bak_RT.trim();
  }
}
//////////////////////////////////////////////////////////////////
void DisplayPty(uint8_t _rdsPty){
  if (_rdsPty != bak_Pty){
    bak_Pty = _rdsPty;
    rds_Pty=TablaPTY(_rdsPty); 
    String _pty=rds_Pty;
    _pty.trim();
    Serial.print(F("PTY -> (")); Serial.print(_rdsPty); Serial.print(F(")")); Serial.println(_pty);
  }
}
//////////////////////////////////////////////////////////////////
void DisplayTpTa(char _rdsTpTa[4]){
  if (String(_rdsTpTa) != rds_TpTa){
    rds_TpTa=String(_rdsTpTa);
    Serial.print(F("TP/TA -> ")); Serial.println(rds_TpTa);
  }
}
//////////////////////////////////////////////////////////////////
ISR(TIMER2_OVF_vect){
// Interrupción: 32,64 mS >>> Refresco: (x7 = 228 mS.) 
  cuenta++;
  if(cuenta >= 7) {
    Refresh_LCD = true;
    cuenta=0;
    punto++;
  }
  //Cambio Grupos/TpTa/PTY >>> (228 x 10 = 2,28 S.) 
  if(punto >= 10){
    punto=0;
    cambio=++cambio;
    if (cambio>=3) cambio=0; 
  }
}
//////////////////////////////////////////////////////////////////
void refrescoLCD(){
  Refresh_LCD = false;
  cuenta2++;

  // Modo: RT/PS
  if (modoRDS==true && cuenta2 >= 150) {
    modoRDS=false;
    cuenta2=0;
  }else if (modoRDS==false && cuenta2 >= 10 && inRT==true){
    modoRDS=true;
    cuenta2=0;
    rds_RT="";
    // Se convierten los caracteres especiales del nuevo RT (LCD >>> UTF-8), antes de enviarlos por el puerto serie
    for (int i=0; i<=(bak_RT.length()); i++){
      // El nuevo RT entra convertido, con los caracteres latinos del LCD
      if (bak_RT[i]<0x10){
        if (bak_RT[i]==0x08) rds_RT = rds_RT + "á";  //á
        if (bak_RT[i]==0x09) rds_RT = rds_RT + "é";  //é
        if (bak_RT[i]==0x0A) rds_RT = rds_RT + "í";  //í
        if (bak_RT[i]==0x0B) rds_RT = rds_RT + "ó";  //ó
        if (bak_RT[i]==0x0C) rds_RT = rds_RT + "ú";  //ú
        if (bak_RT[i]==0x0D) rds_RT = rds_RT + "ñ";  //ñ
        if (bak_RT[i]==0x0E) rds_RT = rds_RT + "ç";  //ç
        if (bak_RT[i]==0x0F) rds_RT = rds_RT + "ü";  //ü
      }else{
        rds_RT = rds_RT + ((char)bak_RT[i]);
      }
    }
    Serial.print(F(">>> RT("));
    Serial.print(bak_RT.length());
    Serial.print(F("): "));
    Serial.println(rds_RT);
    // Se carga el nuevo RT, con los caracteres especiales recibidos (adaptados a la tabla del LCD)
    rds_RT="       " + bak_RT + "        ";
    serialFreeRam();
  }

  // Gestiona el scroll del RT >>> selecciona los 8 caracteres a mostrar, y los guarda en: _rt 
  // El tamaño máximo de la cadena RT = 7 espacios + 64 caracteres + 8 espacios = 79
  String _rt= "";
  if (cuenta2 < 79 && modoRDS ==true)_rt = rds_RT.substring(cuenta2, cuenta2+8);
  
  // Selecciona la presentación PS / RT
  if (inRT==true && modoRDS ==true && _rt != "        " ){
    // Cargará el último RT recibido, después de terminar la presentación
    if (_rt.substring(1, 8) == "       ") cuenta2=150;
    lcd.setCursor(0, 1); 
    lcd.print(_rt);    
  }else{
    // Refresca el PS
    modoRDS=false;
    if (cuenta2 >= 12) cuenta2=0;
    lcd.setCursor(0, 1);
    lcd.print(rds_PS);
  }

  // Indicador de actividad: flecha intermitente (->), o almohadilla fija (#) cuando no se reciban datos RDS
  lcd.setCursor(7, 0); 
  if (_inGR==true){
    _inGR=false;
    InGR=!InGR;
    if (InGR==true){
      lcd.print((char)0x7E);  // ->
    }else{
      lcd.print(F(" "));  
    }
  }else{  // No se reciben datos RDS
    lcd.print(F("#"));  
  }

  // Selecciona lo que se debe mostrar en el LCD: Grupos, TpTa, Pty
  lcd.setCursor(8, 0); 
  if (cambio==0){
    for (int i=0; i<8; i++) lcd.print(gruposRds[i]);    
  }else if (cambio==1){
    lcd.print(F("TP/TA:"));
    lcd.print(rds_TpTa);
  }else{
    lcd.print(rds_Pty);
  }
}
//////////////////////////////////////////////////////////////
void intToCharGroup(uint16_t val) {
  switch (val) {
    case 0: grupo='0'; break;
    case 1: grupo='1'; break;
    case 2: grupo='2'; break;
    case 3: grupo='3'; break;
    case 4: grupo='4'; break;
    case 5: grupo='5'; break;
    case 6: grupo='6'; break;
    case 7: grupo='7'; break;
    case 8: grupo='8'; break;
    case 9: grupo='9'; break;
    case 10: grupo='A'; break;
    case 11: grupo='B'; break;
    case 12: grupo='C'; break;
    case 13: grupo='D'; break;
    case 14: grupo='E'; break;
    case 15: grupo='F'; break;
    default: grupo='-'; 
  }
}
//////////////////////////////////////////////////////////////
const String TablaPTY(uint8_t _pty){
  if (_pty==0) return "  None  ";   //0
  if (_pty==1) return "  News  ";   //1     
  if (_pty==2) return " Affairs";   //2
  if (_pty==3) return "  Info  ";   //3
  if (_pty==4) return "  Sport ";   //4
  if (_pty==5) return " Educate";   //5
  if (_pty==6) return "  Drama ";   //6
  if (_pty==7) return " Culture";   //7
  if (_pty==8) return " Science";   //8
  if (_pty==9) return " Varied ";   //9
  if (_pty==10) return "  Pop M ";  //10
  if (_pty==11) return " Rock M ";  //11
  if (_pty==12) return " Easy M ";  //12
  if (_pty==13) return " Light M";  //13
  if (_pty==14) return "Classics";  //14
  if (_pty==15) return " Other M";  //15
  if (_pty==16) return " Weather";  //16
  if (_pty==17) return " Finance";  //17
  if (_pty==18) return "Children";  //18
  if (_pty==19) return " Social ";  //19
  if (_pty==20) return "Religion";  //20
  if (_pty==21) return "Phone In";  //21
  if (_pty==22) return " Travel ";  //22
  if (_pty==23) return " Leisure";  //23
  if (_pty==24) return "  Jazz  ";  //24
  if (_pty==25) return " Country";  //25
  if (_pty==26) return "Nation M";  //26
  if (_pty==27) return " Oldies ";  //27
  if (_pty==28) return " Folk M ";  //28
  if (_pty==29) return "Document";  //29
  if (_pty==30) return "  TEST  ";  //30
  if (_pty==31) return " Alarm !";  //31
}
/////////////////////////////////////////////////////////
//  23 frecuencias presintonizadas (Madrid Norte)      //   
// Ejemplo: 88.20 MHz >>> 8820 // 104.90 MHz >>> 10490 //
/////////////////////////////////////////////////////////
const uint16_t preset(uint8_t nSet){
  if (nSet==0) return 8820;   // PI:E211 / PS:RNE 1    (CE: Torrespaña)
  if (nSet==1) return 8900;   // PI:E230 / PS:LOS40 CL 
  if (nSet==2) return 9030;   // PI:E215 / PS:RNE 5-M  (CE: Torrespaña)
  if (nSet==3) return 9100;   // PI:E2ED / PS:EUROPAFM
  if (nSet==4) return 9170;   // PI:E237 / PS:DIAL
  if (nSet==5) return 9240;   // PI:E238 / PS:RADIOLE
  if (nSet==6) return 9320;   // PI:E213 / PS:Radio 3  (CE: Torrespaña)
  if (nSet==7) return 9390;   // PI:E235 / PS:LOS40 
  if (nSet==8) return 9510;   // PI:E256 / PS: INTER ··· ECONOMIA
  if (nSet==9) return 9580;   // PI:E213 / PS:Radio 3  (CE: Navacerrada)
  if (nSet==10) return 9650;  // PI:E212 / PS:RNE-CLAS (CE: Torrespaña)
  if (nSet==11) return 9720;  // PI:E2E7 / PS:TOPRADIO
  if (nSet==12) return 9800;  // PI:E2EE / PS:ONDACERO
  if (nSet==13) return 9880;  // PI:E212 / PS:RNE-CLAS (CE: Navacerrada)
  if (nSet==14) return 9950;  // PI:E2CE / PS:CAD-100
  if (nSet==15) return 10070; // PI:E2DC / PS:MegaStar
  if (nSet==16) return 10130; // PI:EDDD / PS:O.MADRID
  if (nSet==17) return 10170; // PI:E2FF / PS:ROCK FM
  if (nSet==18) return 10270; // PI:E2EC / PS:KISS FM
  if (nSet==19) return 10430; // PI:E236 / PS:SER+
  if (nSet==20) return 10490; // PI:E211 / PS:RNE 1    (CE: Navacerrada)
  if (nSet==21) return 10540; // PI:E239 / PS:SER
  if (nSet==22) return 10630; // PI:E2CA / PS:COPE
}
///////////////////////////////////////////////////////////////////////////////////
int freeRam(){
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
///////////////////////////////////////////////////////////////////////////////////
void serialFreeRam(){
  Serial.print(F(">>> Free RAM: ")); Serial.print(freeRam()); Serial.println(F(" Bytes"));
  Serial.println(F("------------------------------------"));
}
//////// FIN //////////////////////////////////////////////////////////////////////
