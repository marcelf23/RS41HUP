// STM32F100 and SI4032 RTTY transmitter
// released under GPL v.2 by anonymous developer
// enjoy and have a nice day
// ver 1.5a
#include <stm32f10x_gpio.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_spi.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_usart.h>
#include <stm32f10x_adc.h>
#include <stm32f10x_rcc.h>
#include "stdlib.h"
#include <stdio.h>
#include <string.h>
#include <misc.h>
#include "f_rtty.h"
#include "init.h"
#include "config.h"
#include "radio.h"
#include "ublox.h"
#include "delay.h"
#include "aprs.h"

char callsign[15] = {CALLSIGN};

#define GREEN  GPIO_Pin_7
#define RED  GPIO_Pin_8

unsigned int send_count;        //frame counter
char status[2] = {'N'};
int voltage;
volatile int adc_bottom = 2000;

volatile char flaga = 0; // GPS Status Flags
volatile int led_enabled = 1; // Flag to disable LEDs at altitude.
uint16_t CRC_rtty = 0x12ab;  //checksum (dummy initial value)
char buf_rtty[200];

volatile unsigned char pun = 0;
volatile unsigned int cun = 10;
volatile unsigned char tx_on = 0;
volatile unsigned int tx_on_delay;
volatile unsigned char tx_enable = 0;
rttyStates send_rtty_status = rttyZero;
volatile char *rtty_buf;
volatile uint16_t button_pressed = 0;
volatile uint8_t disable_armed = 0;

void send_rtty_packet();
uint16_t gps_CRC16_checksum (char *string);


/**
 * GPS data processing
 */
void USART1_IRQHandler(void) {
  if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
    ublox_handle_incoming_byte((uint8_t) USART_ReceiveData(USART1));
      }else if (USART_GetITStatus(USART1, USART_IT_ORE) != RESET) {
    USART_ReceiveData(USART1);
  } else {
    USART_ReceiveData(USART1);
  }
}

void TIM2_IRQHandler(void) {
  if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    if (ALLOW_DISABLE_BY_BUTTON){
      if (ADCVal[1] > adc_bottom){
        button_pressed++;
        if (button_pressed > (RTTY_SPEED / 3)){
          disable_armed = 1;
          GPIO_SetBits(GPIOB, RED);
          //GPIO_SetBits(GPIOB, GREEN);
        }
      } else {
        if (disable_armed){
          GPIO_SetBits(GPIOA, GPIO_Pin_12);
        }
        button_pressed = 0;
      }

    	if (button_pressed == 0) {
    	  adc_bottom = ADCVal[1] * 1.1;	// dynamical reference for power down level
  	  }
    }
      
    if (tx_on) {
      // RTTY Symbol selection logic.
      send_rtty_status = send_rtty((char *) rtty_buf);
      if (!disable_armed){
        if (send_rtty_status == rttyEnd) {
          if (led_enabled) GPIO_SetBits(GPIOB, RED);
          if (*(++rtty_buf) == 0) {
            tx_on = 0;
            tx_on_delay = TX_DELAY / (1000/RTTY_SPEED);
            tx_enable = 0;
            //radio_disable_tx(); // Don't turn off the transmitter!
          }
        } else if (send_rtty_status == rttyOne) {
          radio_rw_register(0x73, RTTY_DEVIATION, 1);
          if (led_enabled) GPIO_SetBits(GPIOB, RED);
        } else if (send_rtty_status == rttyZero) {
          radio_rw_register(0x73, 0x00, 1);
          if (led_enabled) GPIO_ResetBits(GPIOB, RED);
        }
      }
    }

    if (!tx_on && --tx_on_delay == 0) {
      tx_enable = 1;
      tx_on_delay--;
    }

    // Green LED Blinking Logic
    if (--cun == 0) {
      if (pun) {
        // Clear Green LED.
        if (led_enabled) GPIO_SetBits(GPIOB, GREEN);
        pun = 0;
      } else {
        // If we have GPS lock, set LED
        if (flaga & 0x80) {
          if (led_enabled) GPIO_ResetBits(GPIOB, GREEN);
        }
        pun = 1;
      }
      // Wait 200 symbols.
      cun = 200;
    }
  }
}

int main(void) {
#ifdef DEBUG
  debug();
#endif
  RCC_Conf();
  NVIC_Conf();
  init_port();

  init_timer(RTTY_SPEED);
  delay_init();
  ublox_init();

  GPIO_SetBits(GPIOB, RED);
  // NOTE - Green LED is inverted. (Reset to activate, Set to deactivate)
  GPIO_SetBits(GPIOB, GREEN);
  USART_SendData(USART3, 0xc);

  radio_soft_reset();
  // setting RTTY TX frequency
  radio_set_tx_frequency(RTTY_FREQUENCY);

  // setting TX power
  radio_rw_register(0x6D, 00 | (TX_POWER & 0x0007), 1);

  // initial RTTY modulation
  radio_rw_register(0x71, 0x00, 1);

  // Temperature Value Offset
  radio_rw_register(0x13, 0xF0, 1);

  // Temperature Sensor Calibration
  radio_rw_register(0x12, 0x00, 1);

  // ADC configuration
  radio_rw_register(0x0f, 0x80, 1);
  rtty_buf = buf_rtty;
  tx_on = 0;
  tx_enable = 1;

  aprs_init();
  radio_enable_tx();


  while (1) {
    if (tx_on == 0 && tx_enable) {
        send_rtty_packet();
    } else {
      NVIC_SystemLPConfig(NVIC_LP_SEVONPEND, DISABLE);
      __WFI();
    }
  }
}

void send_rtty_packet() {
  start_bits = RTTY_PRE_START_BITS;
  int8_t si4032_temperature = radio_read_temperature();

  voltage = ADCVal[0] * 600 / 4096;
  GPSEntry gpsData;
  ublox_get_last_data(&gpsData);

  uint8_t lat_d = (uint8_t) abs(gpsData.lat_raw / 10000000);
  uint32_t lat_fl = (uint32_t) abs(abs(gpsData.lat_raw) - lat_d * 10000000) / 1000;
  uint8_t lon_d = (uint8_t) abs(gpsData.lon_raw / 10000000);
  uint32_t lon_fl = (uint32_t) abs(abs(gpsData.lon_raw) - lon_d * 10000000) / 1000;

  if (gpsData.fix >= 3) {
      flaga |= 0x80;
      // Disable LEDs if altitude is > 1000m. (Power saving? Maybe?)
      if ((gpsData.alt_raw / 1000) > 1000){
        led_enabled = 0;
      } else {
        led_enabled = 1;
      }
  } else {
      // No GPS fix.
      flaga &= ~0x80;
      led_enabled = 1; // Enable LEDs when there is no GPS fix (i.e. during startup)

      // Null out lat/long data to avoid spamming invalid positions all over the map.
      lat_d = 0;
      lat_fl = 0;
      lon_d = 0;
      lon_fl = 0;
  }
 
  // HORUS RTTY compatible sentences.
  sprintf(buf_rtty, "$$$$$%s,%d,%02u:%02u:%02u,%s%d.%04ld,%s%d.%04ld,%ld,%ld,%d,%d,%d",
			  callsign,
			  send_count,
        gpsData.hours, gpsData.minutes, gpsData.seconds,
        gpsData.lat_raw < 0 ? "-" : "", lat_d, lat_fl,
        gpsData.lon_raw < 0 ? "-" : "", lon_d, lon_fl,
        (gpsData.alt_raw / 1000),
        gpsData.speed_raw,
        gpsData.sats_raw,
        voltage*10,
        si4032_temperature
        );
  CRC_rtty = gps_CRC16_checksum(buf_rtty + 5);
  sprintf(buf_rtty, "%s*%04X\n", buf_rtty, CRC_rtty & 0xffff);
  rtty_buf = buf_rtty;
  radio_enable_tx();
  tx_on = 1;

  send_count++;
}

uint16_t gps_CRC16_checksum(char *string) {
  uint16_t crc = 0xffff;
  char i;
  while (*(string) != 0) {
    crc = crc ^ (*(string++) << 8);
    for (i = 0; i < 8; i++) {
      if (crc & 0x8000)
        crc = (uint16_t) ((crc << 1) ^ 0x1021);
      else
        crc <<= 1;
    }
  }
  return crc;
}


#ifdef  DEBUG
void assert_failed(uint8_t* file, uint32_t line)
{
    while (1);
}
#endif