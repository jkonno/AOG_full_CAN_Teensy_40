  /*
  * USB Autosteer code For AgOpenGPS
  * 4 Feb 2021, Brian Tischler
  * Like all Arduino code - copied from somewhere else :)
  * So don't claim it as your own
  */
  
////////////////// User Settings /////////////////////////  

  //How many degrees before decreasing Max PWM
  #define LOW_HIGH_DEGREES 5.0
  
/////////////////////////////////////////////

  // if not in eeprom, overwrite 
  #define EEP_Ident 5100 

  // Address of CMPS14 shifted right one bit for arduino wire library
  #define CMPS14_ADDRESS 0x60

  // BNO08x definitions
  #define REPORT_INTERVAL 90 //Report interval in ms (same as the delay at the bottom)

  #define CONST_180_DIVIDED_BY_PI 57.2957795130823

  #include <Wire.h>
  #include <EEPROM.h> 
  #include "BNO08x_AOG.h"

  #include <SPI.h>
  #include <FlexCAN_T4.h>
  // Configure this to whatever CAN driver you have hooked up CAN1/CAN2 etc
  FlexCAN_T4<CAN1, RX_SIZE_1024, TX_SIZE_1024> Can0;
  
  void readJ1939WAS(CAN_message_t &msg);
  void readFromBus(CAN_message_t &msg);
  void writeToBus(CAN_message_t &msg);
  
  CAN_message_t flowCommand;
  CAN_message_t wasMessage;
  CAN_message_t keypadMessage;
  
  // Define WAS, flow, button message ID to match from bus structure
  // This stuff works for following config: 
  // Danfoss DST X510 WAS
  // Danfoss PVEA-CI CANBUS valve (or any other CAN valve using standard ISOBUS flow commands)
  // STW M01 J1939 pressure sensor
  // Blinkmarine PKP-2200-LI keypad
  uint32_t wasMessageID = 0x018FF0B15;
  uint32_t steerSwID = 0x018EFFF21;
  uint32_t pressureMessageID = 0x018FF034E;
  uint32_t SASAIIDMessageID = 0x0CFF104D;
  uint32_t flowCommandID = 0x0CFE3022;
  uint32_t keypadMessageID = 0x018EF2100;
  uint16_t oldSteerAngle = 0;
  //End CAN driven valve edit
  
  //loop time variables in microseconds  
  const uint16_t LOOP_TIME = 20;  //50Hz    
  uint32_t lastTime = LOOP_TIME;
  uint32_t currentTime = LOOP_TIME;
  
  const uint16_t WATCHDOG_THRESHOLD = 100;
  const uint16_t WATCHDOG_FORCE_VALUE = WATCHDOG_THRESHOLD + 2; // Should be greater than WATCHDOG_THRESHOLD
  uint8_t watchdogTimer = WATCHDOG_FORCE_VALUE;
  
   //Parsing PGN
  bool isPGNFound = false, isHeaderFound = false;
  uint8_t pgn = 0, dataLength = 0, idx = 0;
  int16_t tempHeader = 0;

  //show life in AgIO
  uint8_t helloAgIO[] = {0x80,0x81, 0x7f, 0xC7, 1, 0, 0x47 };
  uint8_t helloCounter=0;

  //fromAutoSteerData FD 253 - ActualSteerAngle*100 -5,6, Heading-7,8, 
  //Roll-9,10, SwitchByte-11, pwmDisplay-12, CRC 13
  uint8_t AOG[] = {0x80,0x81, 0x7f, 0xFD, 8, 0, 0, 0, 0, 0,0,0,0, 0xCC };
  int16_t AOGSize = sizeof(AOG);

  // booleans to see if we are using CMPS or BNO08x
  bool useCMPS = false;
  bool useBNO08x = false;

  // BNO08x address variables to check where it is
  const uint8_t bno08xAddresses[] = {0x4A,0x4B};
  const int16_t nrBNO08xAdresses = sizeof(bno08xAddresses)/sizeof(bno08xAddresses[0]);
  uint8_t bno08xAddress;
  BNO080 bno08x;

  float bno08xHeading = 0;
  double bno08xRoll = 0;
  double bno08xPitch = 0;

  int16_t bno08xHeading10x = 0;
  int16_t bno08xRoll10x = 0;
  
  //EEPROM
  int16_t EEread = 0;
 
  //Relays
  bool isRelayActiveHigh = true;
  uint8_t relay = 0, relayHi = 0, uTurn = 0;
  uint8_t tram = 0;
  
  //Switches
  uint8_t remoteSwitch = 0, workSwitch = 0, steerSwitch = 1, switchByte = 0;

  //On Off
  uint8_t guidanceStatus = 0;

  //speed sent as *10
  float gpsSpeed = 0;
  
  //steering variables
  float steerAngleActual = 0;
  float steerAngleSetPoint = 0; //the desired angle from AgOpen
  int16_t steeringPosition = 0; //from steering sensor
  float steerAngleError = 0; //setpoint - actual
  
  //pwm variables
  int16_t pwmDrive = 0, pwmDisplay = 0;
  float pValue = 0;
  float errorAbs = 0;
  float highLowPerDeg = 0; 
 
  //Steer switch button  ***********************************************************************************************************
  uint8_t currentState = 1, reading, previous = 0;
  uint8_t pulseCount = 0; // Steering Wheel Encoder
  bool encEnable = false; //debounce flag
  uint8_t thisEnc = 0, lastEnc = 0;

   //Variables for settings  
   struct Storage {
      uint8_t Kp = 40;  //proportional gain
      uint8_t lowPWM = 10;  //band of no action
      int16_t wasOffset = 0;
      uint8_t minPWM = 9;
      uint8_t highPWM = 60;//max PWM value
      float steerSensorCounts = 30;        
      float AckermanFix = 1.0;     //sent as percent
  };  Storage steerSettings;  //14 bytes

   //Variables for settings - 0 is false  
   struct Setup {
      uint8_t InvertWAS = 0;
      uint8_t IsRelayActiveHigh = 0; //if zero, active low (default)
      uint8_t MotorDriveDirection = 0;
      uint8_t SingleInputWAS = 1;
      uint8_t CytronDriver = 1;
      uint8_t SteerSwitch = 0;  //1 if switch selected
      uint8_t SteerButton = 0;  //1 if button selected
      uint8_t ShaftEncoder = 0;
      uint8_t PressureSensor = 0;
      uint8_t CurrentSensor = 0;
      uint8_t PulseCountMax = 5;
      uint8_t IsDanfoss = 0; 
  };  Setup steerConfig;          //9 bytes

  //reset function
  void(* resetFunc) (void) = 0;

  void setup()
  { 
    //set up communication
    Wire.begin();
    Serial.begin(38400);
    delay(400);
  
    // EDIT FOR CAN VALVE STARTS
    // Setup CAN
    Can0.begin();
    Can0.setBaudRate(250000);
    Can0.setMaxMB(16);
    Can0.enableFIFO();
    Can0.enableFIFOInterrupt();
    Can0.onReceive(readFromBus);
    Can0.mailboxStatus();
      
    Serial.println("CAN BUS Init OK!");
    
    // Define valve flow command to bus. Supports MCP and FlexCAN
    flowCommand.id = flowCommandID;
    flowCommand.buf[1] = 0xFF;
    flowCommand.buf[2] = 0xc0;
    flowCommand.flags.extended = 1;
    flowCommand.seq = 1;

    // Define LED state command. Supports MCP and FlexCAN
    keypadMessage.id = keypadMessageID;
    keypadMessage.buf[0] = 0x04; //Header
    keypadMessage.buf[1] = 0x1B; 
    keypadMessage.buf[2] = 0x01; //LED command message
    keypadMessage.buf[3] = 0x01; //Key number
    keypadMessage.buf[4] = 0x01; //LED color
    keypadMessage.buf[5] = 0x01; //LED state
    keypadMessage.buf[6] = 0x00; //LED secondary color
    keypadMessage.buf[7] = 0xFF;
    keypadMessage.flags.extended = 1;
    keypadMessage.seq = 1;
    //EDIT FOR CAN VALVE ENDS

    Can0.write(keypadMessage);
  
    //test if CMPS working
    uint8_t error;
    Wire.beginTransmission(CMPS14_ADDRESS);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.println("Error = 0");
      Serial.print("CMPS14 ADDRESs: 0x");
      Serial.println(CMPS14_ADDRESS, HEX);
      Serial.println("CMPS14 Ok.");
      useCMPS = true;
    }
    else 
    {
      Serial.println("Error = 4");
      Serial.println("CMPS not Connected or Found");
      useCMPS = false;
    }

    // Check for BNO08x
    if(!useCMPS)
    {
      for(int16_t i = 0; i < nrBNO08xAdresses; i++)
      {
        bno08xAddress = bno08xAddresses[i];
        
        Serial.print("\r\nChecking for BNO08X on ");
        Serial.println(bno08xAddress, HEX);
        Wire.beginTransmission(bno08xAddress);
        error = Wire.endTransmission();
    
        if (error == 0)
        {
          Serial.println("Error = 0");
          Serial.print("BNO08X ADDRESs: 0x");
          Serial.println(bno08xAddress, HEX);
          Serial.println("BNO08X Ok.");
          
          // Initialize BNO080 lib        
          if (bno08x.begin(bno08xAddress))
          {
            Wire.setClock(400000); //Increase I2C data rate to 400kHz
  
            // Use gameRotationVector
            bno08x.enableGameRotationVector(REPORT_INTERVAL); //Send data update every REPORT_INTERVAL in ms for BNO085
  
            // Retrieve the getFeatureResponse report to check if Rotation vector report is corectly enable
            if (bno08x.getFeatureResponseAvailable() == true)
            {
              if (bno08x.checkReportEnable(SENSOR_REPORTID_GAME_ROTATION_VECTOR, REPORT_INTERVAL) == false) bno08x.printGetFeatureResponse();

              // Break out of loop
              useBNO08x = true;
              break;
            }
            else 
            {
              Serial.println("BNO08x init fails!!");
            }
          }
          else
          {
            Serial.println("BNO080 not detected at given I2C address.");
          }
        }
        else 
        {
          Serial.println("Error = 4");
          Serial.println("BNO08X not Connected or Found"); 
        }
      }
    }
  
    //50Khz I2C
    TWBR = 144;
  
    EEPROM.get(0, EEread);              // read identifier
      
    if (EEread != EEP_Ident)   // check on first start and write EEPROM
    {           
      EEPROM.put(0, EEP_Ident);
      EEPROM.put(10, steerSettings);   
      EEPROM.put(40, steerConfig);
    }
    else 
    { 
      EEPROM.get(10, steerSettings);     // read the Settings
      EEPROM.get(40, steerConfig);
    }
    
    // for PWM High to Low interpolator
    highLowPerDeg = ((float)(steerSettings.highPWM - steerSettings.lowPWM)) / LOW_HIGH_DEGREES;

  }// End of Setup

  void loop()
  {
  	// Loop triggers every 100 msec and sends back steer angle etc	 
  	currentTime = millis();
   
  	if (currentTime - lastTime >= LOOP_TIME)
  	{
  		lastTime = currentTime;
  
      //reset debounce
      encEnable = true;
     
      //If connection lost to AgOpenGPS, the watchdog will count up and turn off steering
      if (watchdogTimer++ > 250) watchdogTimer = WATCHDOG_FORCE_VALUE;
      
      switchByte = 0;
      switchByte |= (remoteSwitch << 2); //put remote in bit 2
      switchByte |= (steerSwitch << 1);   //put steerswitch status in bit 1 position
      switchByte |= workSwitch;   
      
      if (watchdogTimer < WATCHDOG_THRESHOLD)
      {    
        steerAngleError = steerAngleActual - steerAngleSetPoint;   //calculate the steering error
        //if (abs(steerAngleError)< steerSettings.lowPWM) steerAngleError = 0;
        
        calcSteeringPID();  //do the pid
        motorDrive();       //out to motors the pwm value
      }
      else
      {                
        pwmDrive = 0; //turn off steering motor
        motorDrive(); //out to motors the pwm value
        pulseCount=0;
      }

      //send empty pgn to AgIO to show activity
      if (++helloCounter > 10)
      {
        Serial.write(helloAgIO,sizeof(helloAgIO));
        helloCounter = 0;
      }

      // Set keypad LED
      // Steer swith is OFF --> RED
      if (steerSwitch == 1) { 
          keypadMessage.buf[4] = 0x01;
          Can0.write(keypadMessage);
      } else if (steerSwitch == 0) {
          if (guidanceStatus == 0) {
            // Button pressed but guidance off --> YELLOW
            keypadMessage.buf[4] = 0x04;
            Can0.write(keypadMessage);
          } else {
            // Button pressed, quidance on --> GREEN
            keypadMessage.buf[4] = 0x02;
            Can0.write(keypadMessage);
          }
      }
      
    } //end of timed loop
  
    //This runs continuously, not timed //// Serial Receive Data/Settings /////////////////
  
    // Serial Receive
    //Do we have a match with 0x8081?    
    if (Serial.available() > 1 && !isHeaderFound && !isPGNFound) 
    {
      uint8_t temp = Serial.read();
      if (tempHeader == 0x80 && temp == 0x81) 
      {
        isHeaderFound = true;
        tempHeader = 0;        
      }
      else  
      {
        tempHeader = temp;     //save for next time
        return;    
      }
    }
  
    //Find Source, PGN, and Length
    if (Serial.available() > 2 && isHeaderFound && !isPGNFound)
    {
      Serial.read(); //The 7F or less
      pgn = Serial.read();
      dataLength = Serial.read();
      isPGNFound = true;
      idx=0;
    } 

    //The data package
    if (Serial.available() > dataLength && isHeaderFound && isPGNFound)
    {
      if (pgn == 254) //FE AutoSteerData
      {
        //bit 5,6
        gpsSpeed = ((float)(Serial.read()| Serial.read() << 8 ))*0.1;
        
        //bit 7
        guidanceStatus = Serial.read();

        //Bit 8,9    set point steer angle * 100 is sent
        steerAngleSetPoint = ((float)((short)Serial.read()| (short)(Serial.read() << 8 )))*0.01; //high low bytes
        
        if ((bitRead(guidanceStatus,0) == 0) || (gpsSpeed < 0.1) || (steerSwitch == 1) )
        { 
          watchdogTimer = WATCHDOG_FORCE_VALUE; //turn off steering motor
        }
        else          //valid conditions to turn on autosteer
        {
          watchdogTimer = 0;  //reset watchdog
        }
        
        //Bit 10 Tram 
        tram = Serial.read();
        
        //Bit 11 section 1 to 8
        relay = Serial.read();
        
        //Bit 12 section 9 to 16
        relayHi = Serial.read();
        
        
        //Bit 13 CRC
        Serial.read();
        
        //reset for next pgn sentence
        isHeaderFound = isPGNFound = false;
        pgn=dataLength=0;      

                   //----------------------------------------------------------------------------
        //Serial Send to agopenGPS
        
        int16_t sa = (int16_t)(steerAngleActual*100);
        AOG[5] = (uint8_t)sa;
        AOG[6] = sa >> 8;
        
        if (useCMPS)
        {
          Wire.beginTransmission(CMPS14_ADDRESS);  
          Wire.write(0x02);                     
          Wire.endTransmission();
          
          Wire.requestFrom(CMPS14_ADDRESS, 2); 
          while(Wire.available() < 2);       
        
          //the heading x10
          AOG[8] = Wire.read();
          AOG[7] = Wire.read();
         
          Wire.beginTransmission(CMPS14_ADDRESS);  
          Wire.write(0x1C);                    
          Wire.endTransmission();
         
          Wire.requestFrom(CMPS14_ADDRESS, 2);  
          while(Wire.available() < 2);        
        
          //the roll x10
          AOG[10] = Wire.read();
          AOG[9] = Wire.read();            
        }
        else if(useBNO08x)
        {
          if (bno08x.dataAvailable() == true)
          {
            bno08xHeading = (bno08x.getYaw()) * CONST_180_DIVIDED_BY_PI; // Convert yaw / heading to degrees
            bno08xHeading = -bno08xHeading; //BNO085 counter clockwise data to clockwise data
            
            if (bno08xHeading < 0 && bno08xHeading >= -180) //Scale BNO085 yaw from [-180°;180°] to [0;360°]
            {
              bno08xHeading = bno08xHeading + 360;
            }
                
            bno08xRoll = (bno08x.getRoll()) * CONST_180_DIVIDED_BY_PI; //Convert roll to degrees
            //bno08xPitch = (bno08x.getPitch())* CONST_180_DIVIDED_BY_PI; // Convert pitch to degrees
    
            bno08xHeading10x = (int16_t)(bno08xHeading * 10);
            bno08xRoll10x = (int16_t)(bno08xRoll * 10);
  
            //Serial.print(bno08xHeading10x);
            //Serial.print(",");
            //Serial.println(bno08xRoll10x); 
            
            //the heading x10
            AOG[7] = (uint8_t)bno08xHeading10x;
            AOG[8] = bno08xHeading10x >> 8;
            
    
            //the roll x10
            AOG[9] = (uint8_t)bno08xRoll10x;
            AOG[10] = bno08xRoll10x >> 8;
          }
        }
        else
        { 
          //heading         
          AOG[7] = (uint8_t)9999;        
          AOG[8] = 9999 >> 8;
          
          //roll
          AOG[9] = (uint8_t)8888;  
          AOG[10] = 8888 >> 8;
        }        
        
        AOG[11] = switchByte;
        AOG[12] = (uint8_t)pwmDisplay;
        
        //add the checksum
        int16_t CK_A = 0;
        for (uint8_t i = 2; i < AOGSize - 1; i++)
        {
          CK_A = (CK_A + AOG[i]);
        }
        
        AOG[AOGSize - 1] = CK_A;
        
        Serial.write(AOG, AOGSize);

        // Stop sending the helloAgIO message
        helloCounter = 0;
        //--------------------------------------------------------------------------              
      }
              
      else if (pgn==252) //FC AutoSteerSettings
      {         
        //PID values
        steerSettings.Kp = ((float)Serial.read());   // read Kp from AgOpenGPS
        steerSettings.Kp*=0.5;
        
        steerSettings.highPWM = Serial.read();
        
        steerSettings.lowPWM = Serial.read();   // read lowPWM from AgOpenGPS
                
        steerSettings.minPWM = Serial.read(); //read the minimum amount of PWM for instant on
        
        steerSettings.steerSensorCounts = (float)Serial.read(); //sent as setting displayed in AOG
        
        steerSettings.wasOffset = (Serial.read());  //read was zero offset Hi
               
        steerSettings.wasOffset |= (Serial.read() << 8);  //read was zero offset Lo
        
        steerSettings.AckermanFix = ((float)(Serial.read()));
        steerSettings.AckermanFix*=0.01; 

        //crc
        //udpData[13];        //crc
        Serial.read();
    
        //store in EEPROM
        EEPROM.put(10, steerSettings);           
    
        // for PWM High to Low interpolator
        highLowPerDeg = ((float)(steerSettings.highPWM - steerSettings.lowPWM)) / LOW_HIGH_DEGREES;
        
        //reset for next pgn sentence
        isHeaderFound = isPGNFound = false;
        pgn=dataLength=0;


              
            // Test tare
            //Serial.println("Taring BNO08x");
            //bno08x.tareRotationVectorNow();
            //bno08x.persistTare();
            //Serial.println("Tare done.");
      
      
      }
    
      else if (pgn == 251) //FB - steerConfig
      {       
        uint8_t sett = Serial.read();
         
        if (bitRead(sett,0)) steerConfig.InvertWAS = 1; else steerConfig.InvertWAS = 0;
        if (bitRead(sett,1)) steerConfig.IsRelayActiveHigh = 1; else steerConfig.IsRelayActiveHigh = 0;
        if (bitRead(sett,2)) steerConfig.MotorDriveDirection = 1; else steerConfig.MotorDriveDirection = 0;
        if (bitRead(sett,3)) steerConfig.SingleInputWAS = 1; else steerConfig.SingleInputWAS = 0;
        if (bitRead(sett,4)) steerConfig.CytronDriver = 1; else steerConfig.CytronDriver = 0;
        if (bitRead(sett,5)) steerConfig.SteerSwitch = 1; else steerConfig.SteerSwitch = 0;
        if (bitRead(sett,6)) steerConfig.SteerButton = 1; else steerConfig.SteerButton = 0;
        if (bitRead(sett,7)) steerConfig.ShaftEncoder = 1; else steerConfig.ShaftEncoder = 0;
        
        steerConfig.PulseCountMax = Serial.read();

        //was speed
        Serial.read(); 
        
        sett = Serial.read(); //byte 8 - setting1 - Danfoss valve etc

        if (bitRead(sett, 0)) steerConfig.IsDanfoss = 1; else steerConfig.IsDanfoss = 0;
        if (bitRead(sett, 1)) steerConfig.PressureSensor = 1; else steerConfig.PressureSensor = 0;
        if (bitRead(sett, 2)) steerConfig.CurrentSensor = 1; else steerConfig.CurrentSensor = 0;
              
        Serial.read(); //byte 9
        Serial.read(); //byte 10
         
        Serial.read(); //byte 11
        Serial.read(); //byte 12
      
        //crc byte 13
        Serial.read();
                
        EEPROM.put(40, steerConfig);
      
        //reset for next pgn sentence
        isHeaderFound = isPGNFound = false;
        pgn=dataLength=0; 

        //reset the arduino -- not really needed here...
        //setup();
      }
    
      //clean up strange pgns
      else
      {
          //reset for next pgn sentence
          isHeaderFound = isPGNFound = false;
          pgn=dataLength=0; 
      }        
  
    } //end if (Serial.available() > dataLength && isHeaderFound && isPGNFound)      
    
  } // end of main loop

 // CAN BUS HELPER FUNCTIONS

  void writeToBus(CAN_message_t &msg){
    Can0.write(msg);
  }
  
  void readFromBus(CAN_message_t &msg){
    // WAS message arrives
    if (msg.id == wasMessageID) {
      readJ1939WAS(msg);
    } 
    // Steer button message arrives
    else if (msg.id == steerSwID && msg.buf[3] == 0x01) {
      // Button pressed
      reading = msg.buf[4];      
      if (reading == 0x01 && previous == 0x00) 
      {
        if (currentState == 1)
        {
          currentState = 0;
          steerSwitch = 0;
          //keypadMessage.buf[4] = 0x02;
          //Can0.write(keypadMessage);
        }
      else
        {
          currentState = 1;
          steerSwitch = 1;
          //keypadMessage.buf[4] = 0x01;
          //Can0.write(keypadMessage);
        }
      }      
      previous = reading;
    }
    // Pressure sensor message arrives
    else if (msg.id == pressureMessageID) {
        // Check pressure against threshold
        uint16_t temp = 0;
        temp = (uint16_t) msg.buf[1] << 8;
        temp |= (uint16_t) msg.buf[0];        
        // Scale with 0,004 bar / bit
        int16_t steeringWheelPressureReading = temp * 0.004;
        // When the pressure sensor is reading pressure high enough, shut off
        if (steeringWheelPressureReading >= steerConfig.PulseCountMax)
        {
          steerSwitch = 1; // reset values like it turned off
          currentState = 1;
          previous = 0x00;
        }
    } 
    // SASAIID sensor message arrives
    else if (msg.id == SASAIIDMessageID) {
        // Check steering angle
        uint16_t tempAngle = 0;
        tempAngle = (uint16_t) msg.buf[1] << 8;
        tempAngle |= (uint16_t) msg.buf[0];
        // Check steering angle velocity
        uint16_t tempVelocity = 0;
        tempVelocity = (uint16_t) msg.buf[3] << 8;
        tempVelocity |= (uint16_t) msg.buf[2];  
        // Steer velocity in dRPM (20480 is 300 RPM = 3000 dRPM, PVED-CLS limit is 5 dRPM)
        int16_t steerSpeed = abs((tempVelocity - 20480)*0.146484375);
        // Steering stationary below 5 dRPM
        if (steerSpeed < 5) {
          // Set the stationary angle point
          oldSteerAngle = tempAngle;
        } else {
          uint16_t deltaAngle = abs(tempAngle-oldSteerAngle);
          if (deltaAngle > 2048) {
            deltaAngle = 4096 - deltaAngle; // Handle the 0-360 jump
          }
          // Disengage if angle exceeds 10 degrees (PVED-CLS recommendation)
          if (deltaAngle > 10*4096/360) 
          {
            steerSwitch = 1; // reset values like it turned off
            currentState = 1;
            previous = 0x00;
          }
        }
    }
  }
 
  void readJ1939WAS(CAN_message_t &msg)
  {
    // Check for correct id
    if (msg.id & wasMessageID == wasMessageID){ 
      uint16_t temp = 0;
      temp = (uint16_t) msg.buf[0] << 8;
      temp |= (uint16_t) msg.buf[1];
      steeringPosition = (int)temp;
      //if (steeringPosition > 1800) steeringPosition = steeringPosition - 3600;
      if (steerConfig.InvertWAS)
      {
        steeringPosition = (steeringPosition - steerSettings.wasOffset);   // 1/2 of full scale
        steerAngleActual = (float)(steeringPosition) / -steerSettings.steerSensorCounts;
      }
      else
      {
        steeringPosition = (steeringPosition + steerSettings.wasOffset);   // 1/2 of full scale
        steerAngleActual = (float)(steeringPosition) / steerSettings.steerSensorCounts;
      }
      //Ackerman fix
      if (steerAngleActual < 0) steerAngleActual = (steerAngleActual * steerSettings.AckermanFix);     
    }
  }
