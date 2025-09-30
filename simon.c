#include <ti/devices/msp/msp.h>
#include "lab6_helper.h"
#include <stdlib.h> // for rand() and srand()

uint16_t onTxPacket[] = {0x0000, 0x0000, 0xE5F0, 0x1010, 0xE510,
                         0x10F0, 0xE510, 0xF010, 0xE510, 0x0010, 0xFFFF, 0xFFFF};
uint16_t offTxPacket[] = {0x0000, 0x0000, 0xE000, 0x0000, 0xE000,
                          0x0000, 0xE000, 0x0000, 0xE000, 0x0000, 0xFFFF, 0xFFFF};
uint16_t txMessage[12];
uint16_t *txPacket;

volatile int transmissionComplete = 0; // flag for SPI ISR wakeup
volatile int timerTicked = 0; // flag for 10ms tick (from TIMG0 IRQ)
volatile int idx = 0;
int message_len = sizeof(onTxPacket) / sizeof(onTxPacket[0]);

// Tone load values for different buttons/tones.
#define LOAD_G6 5101  // Green button tone (G6)
#define LOAD_E6 6063  // Red button tone (E6)
#define LOAD_D6 6800  // D6 tone (used in animations)
#define LOAD_C6 7643  // Yellow button tone (C6)
#define LOAD_G5 10206 // Blue button tone (G5)

// Note durations for power-on animation
#define ANIM_DURATION 50  // Duration for each pattern
#define PAUSE_TICKS 10    // Pause between patterns

// Simon game variables
#define MAX_ROUNDS 10
int simonSequence[MAX_ROUNDS]; // Stores the sequence of buttons
int currentRound = 1; // current round (1-indexed; round 1 = 1 element)
int playerInputIndex = 0; // index for player's current expected button
int simonSubState = 0; // 0 = display sequence, 1 = waiting for player input
int inputTimeoutCounter = 0; // Timeout counter (each tick is ~10ms)
#define INPUT_TIMEOUT_TICKS 200 // ~2 seconds
int simon_prevButton = 0; // For tracking button state changes

// Random seed value
unsigned int randVal = 0;

// Power-on pattern state
int powerOnPattern = 1;

// ----------------------------------------------------------------------------
// STATES
typedef enum {
    STATE_PLAY_SONG, // Power-on animation
    SIMON_GAME,      // Simon Says game
    SIMON_WIN,       // Win animation
    SIMON_LOSE       // Game Over
} GameState;

GameState currentState = STATE_PLAY_SONG;

// ----------------------------------------------------------------------------
// FUNCTION PROTOTYPES
void flashButton(int btn, int durationTicks);
void displaySequence(void);
void playAnimation(int isWin);
uint8_t checkButtons(void);

// ----------------------------------------------------------------------------
// checkButtons: Returns 1-4 for a pressed button or 0 if none.
uint8_t checkButtons(void) {
    uint32_t buttons = GPIOA->DIN31_0;
    if ((buttons & SW1) == 0) return 1; // Green
    if ((buttons & SW2) == 0) return 2; // Red
    if ((buttons & SW3) == 0) return 3; // Yellow
    if ((buttons & SW4) == 0) return 4; // Blue
    return 0;
}

// flashButton: flashes the LED and tone for the specified button for a set duration (in ticks).
void flashButton(int btn, int durationTicks) {
    // Set tone based on button
    uint16_t tones[] = {0, LOAD_G6, LOAD_E6, LOAD_C6, LOAD_G5};
    if (btn >= 1 && btn <= 4) {
        setTone(tones[btn]);
    }
    
    // Display the button LED
    generateTxPacket(btn);
    startSPITransmission(txMessage);
    enableBuzzer(1);
    
    // Wait for the specified duration
    waitForTicks(durationTicks, 0);
    
    // Turn off buzzer and LEDs
    enableBuzzer(0);
    startSPITransmission(offTxPacket);
    waitForTicks(PAUSE_TICKS, 0);
}

// displaySequence: for Simon game; plays the sequence up to the current round.
void displaySequence(void) {
    for (int i = 0; i < currentRound; i++) {
        flashButton(simonSequence[i], 50);
    }
}

// Combined win/lose animation - isWin=1 for win animation, 0 for lose
void playAnimation(int isWin) {
    for (int i = 0; i < 3; i++) {
        // Set LED colors - all blue for win, all red for lose
        for (int j = 0; j < 4; j++) {
            if (isWin) {
                txMessage[2 + 2*j] = onTxPacket[2]; // Green
                txMessage[3 + 2*j] = onTxPacket[3];
            } else {
                txMessage[2 + 2*j] = onTxPacket[4]; // Red
                txMessage[3 + 2*j] = onTxPacket[5];
            }
        }
        txMessage[0] = 0x0000;
        txMessage[1] = 0x0000;
        txMessage[10] = 0xFFFF;
        txMessage[11] = 0xFFFF;
        
        // Play appropriate tone
        if (isWin) {
            setTone(LOAD_E6);
        } else {
            setTone(LOAD_C6);
        }
        
        enableBuzzer(1);
        startSPITransmission(txMessage);
        
        // Hold for ~500ms (50 ticks)
        waitForTicks(50, 0);
        enableBuzzer(0);
        
        // Turn off LEDs and pause briefly
        startSPITransmission(offTxPacket);
        waitForTicks(PAUSE_TICKS, 0);
    }
    
    // Reset game to start state
    currentState = STATE_PLAY_SONG;
}

// ----------------------------------------------------------------------------
// INTERRUPT HANDLERS
void SPI0_IRQHandler(void) {
    if (SPI0->CPU_INT.IIDX == SPI_CPU_INT_IIDX_STAT_TX_EVT) {
        SPI0->TXDATA = txPacket[idx];
        idx++;
        if (idx == message_len) {
            transmissionComplete = 1;
            NVIC_DisableIRQ(SPI0_INT_IRQn);
        }
    }
}

void TIMG0_IRQHandler(void) {
    if (TIMG0->CPU_INT.IIDX == GPTIMER_CPU_INT_IIDX_STAT_Z) {
        timerTicked = 1;
    }
}

// ----------------------------------------------------------------------------
// MAIN FUNCTION
int main(void) {
    // Basic initializations.
    InitializeProcessor();
    InitializeGPIO();
    InitializeSPI();
    InitializeTimerG0(); // Timer for 10ms ticks.
    InitializeTimerA1_PWM(); // PWM for buzzer.
    
    enableBuzzer(0);
    
    // Brief startup beep.
    delay_cycles(1600000);
    enableBuzzer(0);
    
    NVIC_EnableIRQ(TIMG0_INT_IRQn);
    TIMG0->COUNTERREGS.LOAD = 327; // ~10ms period.
    TIMG0->COUNTERREGS.CTRCTL |= GPTIMER_CTRCTL_EN_ENABLED;
    
    // Start in power-on animation state.
    while (1) {
        uint8_t btn = checkButtons();
        
        switch (currentState) {
            case STATE_PLAY_SONG: {
                // Increment randVal every cycle to increase randomness.
                randVal++;
                
                // If any button is pressed, wait for release and then transition.
                if (btn != 0) {
                    while(checkButtons() != 0) { }
                    
                    srand(randVal);
                    currentState = SIMON_GAME;
                    currentRound = 1;
                    playerInputIndex = 0;
                    simonSubState = 0; // Next, display the sequence.
                    inputTimeoutCounter = 0;
                    simonSequence[0] = (rand() % 4) + 1;
                    enableBuzzer(0);
                    break;
                }
                
                // Otherwise, animate the LED patterns.
                displayPattern(powerOnPattern);
                
                // Select tone based on pattern
                uint16_t patternTones[] = {0, LOAD_G6, LOAD_E6, LOAD_C6};
                setTone(patternTones[powerOnPattern]);
                enableBuzzer(1);
                
                // Start SPI transmission
                startSPITransmission(txMessage);
                
                // Wait for animation duration, checking for button press
                waitForTicks(ANIM_DURATION, 1);
                if (currentState == SIMON_GAME)
                    break;
                
                // Turn off LEDs and tone
                enableBuzzer(0);
                startSPITransmission(offTxPacket);
                
                // Wait for pause duration, checking for button press
                waitForTicks(PAUSE_TICKS, 1);
                if (currentState == SIMON_GAME)
                    break;
                
                // Cycle to next pattern
                powerOnPattern = (powerOnPattern % 3) + 1;
                break;
            }
            
            case SIMON_GAME: {
                if (simonSubState == 0) {
                    // Wait until all buttons are released before displaying the sequence.
                    if (checkButtons() != 0) {
                        break; // wait until all are released.
                    }
                    
                    // Display the sequence for the current round.
                    displaySequence();
                    simonSubState = 1; // Now wait for player input.
                    playerInputIndex = 0;
                    inputTimeoutCounter = 0;
                    simon_prevButton = 0;
                }
                else if (simonSubState == 1) {
                    int currentButton = checkButtons();
                    
                    if (currentButton != 0) {
                        // If this is a new press, process it.
                        if (simon_prevButton == 0) {
                            simon_prevButton = currentButton; // register new press
                            flashButton(currentButton, 20); // give immediate feedback
                            
                            // Check if the pressed button matches the expected button.
                            if (currentButton != simonSequence[playerInputIndex]) {
                                currentState = SIMON_LOSE;
                                break;
                            }
                            else {
                                playerInputIndex++;
                                inputTimeoutCounter = 0; // reset timeout
                                
                                // If the sequence for this round is complete...
                                if (playerInputIndex >= currentRound) {
                                    if (currentRound >= MAX_ROUNDS) {
                                        currentState = SIMON_WIN;
                                        break;
                                    }
                                    else {
                                        currentRound++;
                                        simonSequence[currentRound - 1] = (rand() % 4) + 1;
                                        simonSubState = 0; // prepare to display new sequence
                                    }
                                    simon_prevButton = 0; // reset edge detector for next round
                                }
                            }
                        }
                        // If button is continuously held, also check that it still matches.
                        else if (currentButton != simon_prevButton) {
                            // If the held button changed, register that change.
                            simon_prevButton = currentButton;
                            flashButton(currentButton, 20);
                            
                            if (currentButton != simonSequence[playerInputIndex]) {
                                currentState = SIMON_LOSE;
                                break;
                            }
                        }
                    }
                    else {
                        // No button pressed; reset edge detector.
                        simon_prevButton = 0;
                    }
                    
                    // Update the timeout counter.
                    if (timerTicked) {
                        inputTimeoutCounter++;
                        timerTicked = 0;
                    }
                    
                    if (inputTimeoutCounter > INPUT_TIMEOUT_TICKS) {
                        currentState = SIMON_LOSE;
                        break;
                    }
                }
                break;
            }
            
            case SIMON_WIN:
                playAnimation(1); // Play win animation
                break;
            
            case SIMON_LOSE:
                playAnimation(0); // Play lose animation
                break;
            
            // For other states, turn LEDs off.
            default: {
                startSPITransmission(offTxPacket);
                waitForTicks(1, 0);
                break;
            }
        }
    }
    
    return 0;
}