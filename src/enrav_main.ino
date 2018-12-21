#include "Arduino.h"
#include <SPI.h>
#include <WiFi.h>
#include "vs1053_ext.h"
#include "mp3player.h"
#include "UserInterface.h"

#include "pinout.h"

#include "SimpleCLI.h"
using namespace simplecli;

#ifdef ARDUINO_ARCH_ESP32
    #include "esp32-hal-log.h"    
#else
    static const char *TAG = "main";
#endif

#if __has_include("credentials.h")
    #include "credentials.h"
#endif

#ifndef __CREDENTIALS__H
    String ssid =     "WLAN";
    String password = "xxxxxxxxxxxxxxxx";
#endif

UserInterface   myInterface;
Mp3player       MyPlayer(VS1053_CS, VS1053_DCS, VS1053_DREQ);

QueueHandle_t     PlayerCommandQueue;
QueueHandle_t   *pCommandInterfaceQueue;

//
SimpleCLI       *pCli;          // pointer to command line handler
String          NewCommand;     // string to collect characters from the input

String Version = "EnRav 0.12.0";

//The setup function is called once at startup of the sketch
void setup() {

    PlayerCommandQueue = xQueueCreate( 5, sizeof( Mp3player::PlayerControlMessage_s ) );

    Serial.begin(115200);

    SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI);

    //prepare the user interface    
    myInterface.setPlayerCommandQueue(&PlayerCommandQueue);
    pCommandInterfaceQueue = myInterface.getInterfaceCommandQueue();
    myInterface.begin();

    SD.begin(SDCARD_CS);

//    MyPlayer.begin(&PlayerCommandQueue);

    // WiFi.disconnect();
    // WiFi.mode(WIFI_STA);
    // WiFi.begin(ssid.c_str(), password.c_str());
    // while (WiFi.status() != WL_CONNECTED) delay(1500);

    CommandLine_create();

    Serial.println(Version);
}


// The loop function is called in an endless loop
void loop()
{
    // read serial input
    while(Serial.available()){
        char tmp;
        
        if (Serial.readBytes(&tmp, 1) >= 1) 
        {
            Serial.print(tmp);

            NewCommand += String(tmp);

            if (tmp == '\n')
            {
               pCli->parse(NewCommand); 
               NewCommand = String();
               Serial.print("> ");
            }
        }
    }
}


void CommandLine_create(void)
{
    // =========== Create CommandParser =========== //
    pCli = new SimpleCLI();

    // when no valid command could be found for given user input
    pCli->onNotFound = [](String str) {
                          Serial.println("\"" + str + "\" not found");
                      };
    // ============================================ //

    // =========== Add stop command ========== //
    pCli->addCmd(new EmptyCmd("help", [](Cmd* cmd) {
        
        Serial.println("EnRav Audio player (by M. Reinecke)");
        Serial.println("-----------------------------------");
        Serial.println("available commands:");
        Serial.println("- version               : show firmware version");
        Serial.println("");
        Serial.println("- play <filename>       : start playing the given file name (must be a mp3 or playlist)");
        Serial.println("- stop                  : stops the actual playback");
        Serial.println("");
        Serial.println("- volume <1..100>       : set volume to level");
        Serial.println("  volume up             : increase volume by 5 steps");
        Serial.println("  volume down           : decrease volume by 5 steps");
        Serial.println("");
        Serial.println(" - write <filename>     :  setup RFID card with the given parameters");
    }));
    // ======================================== //

    // =========== Add version info command ========== //
    pCli->addCmd(new Command("version", [](Cmd* cmd) {
        Serial.println(Version);
    }));
    // ======================================== //

    // =========== Add play command ========== //
    pCli->addCmd(new SingleArgCmd("play", [](Cmd* cmd) {        
        String *pFileName = new String(cmd->getValue(0));

        UserInterface::InterfaceCommandMessage_s newMessage =   { 
                                                                .Command    = UserInterface::CMD_PLAY_FILE,
                                                                .pData      = pFileName 
                                                                };
        // the message is copied to the queue, so no need for the original one :)
        if (!xQueueSend( *pCommandInterfaceQueue, &newMessage, ( TickType_t ) 0 ) )
        {
            ESP_LOGE(TAG, "Send to queue failed");
            delete pFileName;
        }
    }));
    // ======================================== //

    // =========== Add stop command ========== //
    pCli->addCmd(new EmptyCmd("stop", [](Cmd* cmd) {
        UserInterface::InterfaceCommandMessage_s newMessage = { .Command = UserInterface::CMD_PLAY_STOP };

        // the message is copied to the queue, so no need for the original one :)
        if (!xQueueSend( *pCommandInterfaceQueue, &newMessage, ( TickType_t ) 0 ) )
        {
            ESP_LOGE(TAG, "Send to queue failed");
        }
    }));
    // ======================================== //

    // =========== Add volume command ========== //
    pCli->addCmd(new SingleArgCmd("volume", [](Cmd* cmd) {        
        // String *pFileName = new String(cmd->getValue(0));
    }));
    // ======================================== //

    // =========== Add write command ========== //
    Command* writeCard = new Command("write", [](Cmd* cmd) {
        String fileName = cmd->getValue(0);

        if (fileName.length() > 0) 
        {
            UserInterface::InterfaceCommandMessage_s newMessage;
            CardData  *pNewCard = new CardData();

            //save the data to the stucture
            pNewCard->m_fileName = fileName;

            //and fill the message to the interface
            newMessage.Command    = UserInterface::CMD_CARD_WRITE;
            newMessage.pData      = pNewCard ;

            // the message is copied to the queue, so no need for the original one :)
            if (!xQueueSend( *pCommandInterfaceQueue, &newMessage, ( TickType_t ) 0 ) )
            {
                ESP_LOGE(TAG, "Send to queue failed");
                delete pNewCard;
            }            
        } 
        else 
        {
            Serial.println("Illegal parameter (FileName)");
        }
    });    
    writeCard->addArg(new AnonymReqArg());
    pCli->addCmd(writeCard);
    // ======================================== //

    // =========== Add change log level command ========== //
    pCli->addCmd(new SingleArgCmd("log", [](Cmd* cmd) {  
        String data = cmd->getValue(0);

        if (data.equalsIgnoreCase("DEBUG"))
        {
            esp_log_level_set("*", ESP_LOG_DEBUG);
        } 
        else if (data.equalsIgnoreCase("VERBOSE"))
        {
            esp_log_level_set("*", ESP_LOG_VERBOSE);
        } 
        else if (data.equalsIgnoreCase("WARNING"))
        {
            esp_log_level_set("*", ESP_LOG_WARN);
        } 
        else if (data.equalsIgnoreCase("INFO"))
        {
            esp_log_level_set("*", ESP_LOG_INFO);
        }
    }));
    // ======================================== //    
}

//     // Check for compatibility
//     if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
//         &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
//         &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
//         Serial.println(F("This sample only works with MIFARE Classic cards."));
//         goto CleanUp;
//     }
// {
//     // In this sample we use the second sector,
//     // that is: sector #1, covering block #4 up to and including block #7
//     byte sector         = 1;
//     byte blockAddr      = 4;
//     byte dataBlock[]    = {
//         0x01, 0x02, 0x03, 0x04, //  1,  2,   3,  4,
//         0x05, 0x06, 0x07, 0x08, //  5,  6,   7,  8,
//         0x09, 0x0a, 0xff, 0x0b, //  9, 10, 255, 11,
//         0x0c, 0x0d, 0x0e, 0x0f  // 12, 13, 14, 15
//     };
//     byte trailerBlock   = 7;
//     MFRC522::StatusCode status;
//     byte buffer[18];
//     byte size = sizeof(buffer);

//     // Authenticate using key A
//     Serial.println(F("Authenticating using key A..."));
//     status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("PCD_Authenticate() failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//         return;
//     }

//     // Show the whole sector as it currently is
//     Serial.println(F("Current data in sector:"));
//     mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
//     Serial.println();

//     // Read data from the block
//     Serial.print(F("Reading data from block ")); Serial.print(blockAddr);
//     Serial.println(F(" ..."));
//     status = mfrc522.MIFARE_Read(blockAddr, buffer, &size);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Read() failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     Serial.print(F("Data in block ")); Serial.print(blockAddr); Serial.println(F(":"));
//     dump_byte_array(buffer, 16); Serial.println();
//     Serial.println();

//     //prepare a dummy Tag
//     memset(myTag.Target.DataPath, 0, sizeof(myTag.Target.DataPath));
//     strcpy(myTag.Target.DataPath, "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod");

//     ESP_LOGD(TAG, "Writing data into block 4..6, 8..10");

//     ESP_LOGD(TAG, "writing info block 4 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(4, myTag.Information.Raw, 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(4) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");

//     ESP_LOGD(TAG, "writing info block 5 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(5, myTag.Target.Raw[0], 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(5) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");

//     ESP_LOGD(TAG, "writing info block 6 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(6, myTag.Target.Raw[1], 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(5) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");

//     // Authenticate using key A
//     Serial.println(F("Authenticating using key A..."));
//     status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 10, &key, &(mfrc522.uid));
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("PCD_Authenticate() failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//         return;
//     }

//     ESP_LOGD(TAG, "writing info block 8 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(8, myTag.Target.Raw[2], 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(8) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");

//     ESP_LOGD(TAG, "writing info block 9 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(9, myTag.Target.Raw[3], 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(9) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");

//             ESP_LOGD(TAG, "writing info block 10 ..");
//     status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(10, myTag.Target.Raw[4], 16);
//     if (status != MFRC522::STATUS_OK) {
//         Serial.print(F("MIFARE_Write(10) failed: "));
//         Serial.println(mfrc522.GetStatusCodeName(status));
//     }
//     ESP_LOGD(TAG, "done");
    // PlayerControlMessage_s myMessage = { .Command = CMD_UNKNOWN };

    // if (mfrc522.uid.size >= 2) 
    // {
    //     if ((mfrc522.uid.uidByte[0] == 0x04) && (mfrc522.uid.uidByte[1] == 0x65))
    //     {
    //         myMessage.Command       = CMD_PLAY_FILE;
    //         myMessage.pFileToPlay   = (char *) malloc (16 * sizeof(char));
    //         strncpy(myMessage.pFileToPlay, "/01.mp3", 16); 
    //     }
    //     else if ((mfrc522.uid.uidByte[0] == 0x04) && (mfrc522.uid.uidByte[1] == 0xE8))
    //     {
    //         myMessage.Command       = CMD_PLAY_FILE;
    //         myMessage.pFileToPlay   = (char *) malloc (16 * sizeof(char));
    //         strncpy(myMessage.pFileToPlay, "/02.mp3", 16); 
    //     }
    //     else if ((mfrc522.uid.uidByte[0] == 0x04) && (mfrc522.uid.uidByte[1] == 0x01))
    //     {
    //         myMessage.Command       = CMD_STOP;
    //         myMessage.pFileToPlay   = NULL;
    //     }
    // }

    // if (myMessage.Command != CMD_UNKNOWN) 
    // {
    //     if (xQueueSend( *pPlayerQueue, &myMessage, ( TickType_t ) 0 ) )
    //     {
    //         ESP_LOGD(TAG, "send to queue successfull");
    //     } else {
    //         ESP_LOGD(TAG, "send to queue failed");

    //         //if the send failed, we must do the job
    //         free(myMessage.pFileToPlay);
    //     }
    // }
// }
