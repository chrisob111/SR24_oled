#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "tusb.h"
#include "u8g2.h"
#include "pico/bootrom.h"

// Puffer für empfangene Daten
static uint8_t buf[256];

// U8g2 Instanz
u8g2_t u8g2;

// Globale Variablen für Fahrzeugdaten
int vcu_soc = 0;
float vcu_voltage = 0.0;
int vcu_speed = 0;
int vcu_hw_version = 0;
char vcu_sw_version[32] = "";
char vehicle_type[32] = "";
int battery_design_voltage = 0;
int battery_design_capacity = 0;
char battery_cell_type[32] = "";
float bms_current = 0.0;
int bms_soc = 0;
int bms_max_temp = 0;
int bms_min_temp = 0;
float bms_voltage = 0.0;
int bms_charge_status = 0;
int bms_max_cell_diff = 0;
int ctrl_current = 0;
int ctrl_voltage = 0;
int ctrl_throttle = 0;
int ctrl_temp = 0;
int ctrl_motor_temp = 0;
int ocv_state = 0;
int vcu_soc_voltage = 0;
int vcu_throttle_voltage = 0;
char engine_state[32] = "";
int out_dcdc_en = 0;
int out_discharge = 0;
int out_mcu_limp = 0;
int out_ctrl_en = 0;
int out_ctrl_reverse = 0;
int out_brake = 0;
int out_mosfet_charge_port = 0;
int out_boot0 = 0;
int out_throttle = 0;
int power_limit_soc = 0;
int power_limit_temp = 0;
int max_discharge_current = 0;
int max_recharge_current = 0;
int voltage_good = 0;
int voltage_standby = 0;
int voltage_low = 0;
int consumption = 0;
int remaining_distance = 0;
float power = 0.0; 

// Globale Variable für Anzeigemodus
int current_display_mode = 0; // 0 = Standard, 1 = Consumption
const uint BUTTON_PIN = 21;   // Button an GPIO 21

// Zeitpunkt der letzten Display-Aktualisierung
uint32_t last_display_update = 0;

// Zeilenpuffer für Serial-Parser
static char rx_line_buf[128];
static uint8_t rx_line_pos = 0;
// Sommerzeit- und Winterzeit-Daten bis 2035
static const int s26 = 20260329; static const int w26 = 20261025;
static const int s27 = 20270328; static const int w27 = 20271031;
static const int s28 = 20280326; static const int w28 = 20281029;
static const int s29 = 20290325; static const int w29 = 20291028;
static const int s30 = 20300331; static const int w30 = 20301027;
static const int s31 = 20310330; static const int w31 = 20311026;
static const int s32 = 20320328; static const int w32 = 20321031;
static const int s33 = 20330327; static const int w33 = 20331030;
static const int s34 = 20340326; static const int w34 = 20341029;
static const int s35 = 20350325; static const int w35 = 20351028;
// --- GPS & Zeit Logik ---

// Minimaler GPS Parser, der TinyGPS++ simuliert
class TinyGPSPlus {
public:
    struct {
        uint16_t y; uint8_t m, d; bool valid;
        uint16_t year() { return y; }
        uint8_t month() { return m; }
        uint8_t day() { return d; }
        bool isValid() { return valid; }
    } date;
    struct {
        uint8_t h, min, s; bool valid;
        uint8_t hour() { return h; }
        uint8_t minute() { return min; }
        bool isValid() { return valid; }
    } time;
    struct {
        double val; bool valid;
        double kmph() { return val * 1.852; } // Knoten in km/h
        bool isValid() { return valid; }
    } speed;
    struct {
        int value; bool valid;
        int count() { return value; }
        bool isValid() { return valid; }
    } satellites;

    void parse(char* line) {
        // Checksumme prüfen
        if (line[0] != '$') return;
        char* ast = strchr(line, '*');
        if (!ast) return;
        uint8_t sum = 0;
        for (char* p = line + 1; p < ast; p++) sum ^= *p;
        if (strtol(ast + 1, NULL, 16) != sum) return;
        *ast = 0; 

        // RMC (Zeit, Datum, Speed) und GGA (Satelliten) parsen
        bool isRMC = (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0);
        bool isGGA = (strncmp(line, "$GPGGA", 6) == 0 || strncmp(line, "$GNGGA", 6) == 0);
        if (!isRMC && !isGGA) return;

        // Tokenizer
        char* tokens[15];
        int count = 0;
        char* p = line;
        while (*p && count < 15) {
            tokens[count++] = p;
            while (*p && *p != ',') p++;
            if (*p) *p++ = 0;
        }
        if (count < 10) return;

        if (isRMC) {
            // Zeit: HHMMSS.ss
            if (strlen(tokens[1]) >= 6) {
                char tmp[3]; tmp[2] = 0;
                tmp[0] = tokens[1][0]; tmp[1] = tokens[1][1]; time.h = atoi(tmp);
                tmp[0] = tokens[1][2]; tmp[1] = tokens[1][3]; time.min = atoi(tmp);
                tmp[0] = tokens[1][4]; tmp[1] = tokens[1][5]; time.s = atoi(tmp);
                time.valid = true;
            }

            // Status prüfen (A = Active/Valid)
            bool fix = (strcmp(tokens[2], "A") == 0);

            // Geschwindigkeit: Knoten
            if (fix && tokens[7][0]) {
                speed.val = atof(tokens[7]);
                speed.valid = true;
            } else {
                speed.valid = false;
            }

            // Datum: DDMMYY
            if (strlen(tokens[9]) == 6) {
                char tmp[3]; tmp[2] = 0;
                tmp[0] = tokens[9][0]; tmp[1] = tokens[9][1]; date.d = atoi(tmp);
                tmp[0] = tokens[9][2]; tmp[1] = tokens[9][3]; date.m = atoi(tmp);
                tmp[0] = tokens[9][4]; tmp[1] = tokens[9][5]; date.y = 2000 + atoi(tmp);
                date.valid = true;
            }
        } else if (isGGA) {
            // Satellitenanzahl steht in Feld 7 bei GGA
            if (tokens[7][0]) {
                satellites.value = atoi(tokens[7]);
                satellites.valid = true;
            }
        }
    }
};

// GPS Instanz und Puffer
TinyGPSPlus gps;
static char gps_line_buf[128];
static uint8_t gps_line_pos = 0;

void calculateTimeStr(TinyGPSPlus &gps, char *timeStr, size_t len)
{
    if (len < 6) return;

    if (gps.time.isValid() && gps.date.isValid())
    {
        // Datum prüfen für Sommerzeit
        char datum[20];
        // WICHTIG: %02d für Monat/Tag, damit z.B. 20260329 entsteht und nicht 2026329
        snprintf(datum, sizeof(datum), "%04d%02d%02d", gps.date.year(), gps.date.month(), gps.date.day());
        int dat = (int)strtol(datum, NULL, 10);

        int zeit = 1; // Normalzeit (Winterzeit)
        if ((dat > s26 && dat < w26) || (dat >= s27 && dat < w27) ||
            (dat >= s28 && dat < w28) || (dat >= s29 && dat < w29) ||
            (dat >= s30 && dat < w30) || (dat >= s31 && dat < w31) ||
            (dat >= s32 && dat < w32) || (dat >= s33 && dat < w33) ||
            (dat >= s34 && dat < w34) || (dat >= s35 && dat < w35))
        {
            zeit = 2; // Sommerzeit
        }

        int hour = gps.time.hour() + zeit;
        if (hour >= 24) hour -= 24;
        int minute = gps.time.minute();

        snprintf(timeStr, len, "%02d:%02d", hour, minute);
    }
    else
    {
        snprintf(timeStr, len, "--:--");
    }
}

void process_gps_line(char* line) {
    gps.parse(line);
}

// I2C Callback für U8g2 (Hardware I2C0 auf GP4/GP5)
uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[256];
    static uint8_t buf_idx;
    uint8_t *data;

    switch(msg) {
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            while( arg_int > 0 ) {
                if (buf_idx < sizeof(buffer)) {
                    buffer[buf_idx++] = *data;
                }
                data++;
                arg_int--;
            }
            break;
        case U8X8_MSG_BYTE_INIT:
            break;
        case U8X8_MSG_BYTE_SET_DC:
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_write_blocking(i2c0, u8x8_GetI2CAddress(u8x8) >> 1, buffer, buf_idx, false);
            break;
        default:
            return 0;
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            break;
        case U8X8_MSG_DELAY_MILLI:
            sleep_ms(arg_int);
            break;
        default:
            return 0;
    }
    return 1;
}

void oled_status(const char* msg) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawStr(&u8g2, 30, 12, "Tacho 3000");
    u8g2_DrawStr(&u8g2, 30, 28, msg);
    u8g2_SendBuffer(&u8g2);
}

// Hilfsfunktion zum Zeichnen von Zahlen mit festen Ziffernpositionen
void draw_number_fixed(int value, int x_anchor, int y, int spacing) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    int len = strlen(buf);
    int digit_width = u8g2_GetStrWidth(&u8g2, "0");
    int slot_width = digit_width + spacing;
    
    for (int i = 0; i < len; i++) {
        // Von rechts (Einer) nach links verarbeiten
        int char_idx = len - 1 - i;
        char digit_str[2] = { buf[char_idx], '\0' };
        int w = u8g2_GetStrWidth(&u8g2, digit_str);
        // Rechtsbündig im Slot zeichnen
        u8g2_DrawStr(&u8g2, x_anchor - (i * slot_width) - w, y, digit_str);
    }
}

// Hilfsfunktion um Floats mit fester Ziffernposition zu zeichnen
void draw_float_fixed(float value, int decimals, int x_anchor, int y) {
    char buf[32];
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    snprintf(buf, sizeof(buf), fmt, value);
    
    int len = strlen(buf);
    int digit_width = u8g2_GetStrWidth(&u8g2, "0");
    int spacing = 1;
    int slot_width = digit_width + spacing;
    
    int current_x = x_anchor;
    
    for (int i = len - 1; i >= 0; i--) {
        char c = buf[i];
        char str[2] = {c, '\0'};
        int w = u8g2_GetStrWidth(&u8g2, str);
        
        u8g2_DrawStr(&u8g2, current_x - w, y, str);
        
        if (c >= '0' && c <= '9') {
            current_x -= slot_width;
        } else {
            current_x -= (w + spacing);
        }
    }
}

// Funktion zur Anzeige der Fahrzeugdaten auf mehreren Bildschirmen
void update_vehicle_display() {
    u8g2_ClearBuffer(&u8g2);
    
    if (current_display_mode == 1) {
        u8g2_DrawHLine(&u8g2, 3, 0, 128);
        u8g2_DrawVLine(&u8g2, 3, 1, 3);
        u8g2_DrawVLine(&u8g2, 28, 1, 7);
        u8g2_DrawVLine(&u8g2, 53, 1, 3);
        u8g2_DrawVLine(&u8g2, 78, 1, 3);
        u8g2_DrawVLine(&u8g2, 103, 1, 3);
        u8g2_DrawVLine(&u8g2, 127, 1, 3);
        power = roundf(bms_voltage * bms_current / 40);
        if (power == 0){
            u8g2_DrawVLine(&u8g2, 28, 10 , 4);
        }
        else if (power < 0) {
            power = fabs(power);
            u8g2_DrawBox(&u8g2, 28, 10 , power, 4);
        }
        else {
            u8g2_DrawBox(&u8g2, 28 - power, 10 , power, 4);
        }
        u8g2_SetFont(&u8g2, u8g2_font_helvB10_tf);
        //u8g2_DrawStr(&u8g2, 2, 32, "V:");
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", consumption);
        u8g2_DrawStr(&u8g2, 20, 32, buf);

        // Einheit "Wh/km" mit kleinerer Schrift hinzufügen
        int w = u8g2_GetStrWidth(&u8g2, buf);
        u8g2_SetFont(&u8g2, u8g2_font_5x7_tr);
        u8g2_DrawStr(&u8g2, 20 + w + 2, 32, "Wh/km");

        // Leistung in kW rechts daneben
        float power_kw = (bms_voltage * bms_current) / 1000.0f;
        char pwr_buf[16];
        snprintf(pwr_buf, sizeof(pwr_buf), "%.1f", power_kw);
        int w_unit = u8g2_GetStrWidth(&u8g2, "kW");
        u8g2_DrawStr(&u8g2, 128 - w_unit, 32, "kW");
        
        u8g2_SetFont(&u8g2, u8g2_font_helvB10_tf);
        int w_pwr = u8g2_GetStrWidth(&u8g2, pwr_buf);
        u8g2_DrawStr(&u8g2, 128 - w_unit - w_pwr - 2, 32, pwr_buf);
    } else if (current_display_mode == 2) {
        // Modus 2: Spannung und Strom
        u8g2_SetFont(&u8g2, u8g2_font_helvB12_tf);
        draw_float_fixed(bms_voltage, 2, 82, 14);
        u8g2_DrawStr(&u8g2, 86, 14, "V");
        
        draw_float_fixed(bms_current, 2, 82, 30);
        u8g2_DrawStr(&u8g2, 86, 30, "A");
    } else {
    // Standard Anzeige
    int w;
    // Geschwindigkeit links groß (nur Zahlen)
    u8g2_SetFont(&u8g2, u8g2_font_logisoso32_tn);
    
    // Wenn GPS-Geschwindigkeit gültig ist, diese verwenden, sonst VCU
    int display_speed = vcu_speed;
    if (gps.speed.isValid()) {
        display_speed = (int)gps.speed.kmph();
    }
    draw_number_fixed(display_speed, 55, 32, 2);
    
    // Zurück zu kleinerer Schrift für den Rest (tf für erweiterten Zeichensatz inkl. Grad)
    u8g2_SetFont(&u8g2, u8g2_font_helvB10_tf);
    
    // Temperatur mittig oben (Zahlen rechtsbündig an X=75)
    draw_number_fixed(bms_max_temp, 75, 12, 1);
    u8g2_DrawStr(&u8g2, 77, 12, "\xb0");
    
    // Reichweite mittig unten (Zahlen rechtsbündig an X=75)
    draw_number_fixed(remaining_distance / 1000, 75, 32, 1);
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tr);
    u8g2_DrawStr(&u8g2, 77, 32, "km");
    u8g2_SetFont(&u8g2, u8g2_font_helvB10_tf);
    
    // Uhrzeit rechts oben (größer, gleiche Schrift)
    char time_str[10];
    calculateTimeStr(gps, time_str, sizeof(time_str));
    w = u8g2_GetStrWidth(&u8g2, time_str);
    u8g2_DrawStr(&u8g2, 128 - w, 12, time_str);
    
    // Satellitenanzahl rechts unten (klein)
    if (gps.satellites.isValid()) {
        //u8g2_SetFont(&u8g2, u8g2_font_5x7_tr);
        char sat_buf[8];
        snprintf(sat_buf, sizeof(sat_buf), "%d", gps.satellites.count());
        int w_sat = u8g2_GetStrWidth(&u8g2, sat_buf);
        u8g2_DrawStr(&u8g2, 128 - w_sat, 32, sat_buf);
    }
    }

    u8g2_SendBuffer(&u8g2);
}

// Funktion zum Invertieren der Display-Farben (Negativbild)
void set_display_invert(bool invert) {
    // 0xA7 = Invertiert (Hintergrund hell), 0xA6 = Normal (Hintergrund dunkel)
    u8g2_SendF(&u8g2, "c", invert ? 0xA7 : 0xA6);
}

// Parser für eine empfangene Zeile
void process_line(char* line) {
    // Entferne führende Leerzeichen
    while(*line == ' ') line++;

    if (strncmp(line, "VCU HW Version:", 15) == 0) {
        vcu_hw_version = atoi(line + 15);
    }
    else if (strncmp(line, "VCU SW Version:", 15) == 0) {
        char* p = line + 15; while(*p == ' ') p++; // Leerzeichen überspringen
        strncpy(vcu_sw_version, p, sizeof(vcu_sw_version) - 1);
        vcu_sw_version[sizeof(vcu_sw_version) - 1] = 0;
    }
    else if (strncmp(line, "Vehicle type:", 13) == 0) {
        char* p = line + 13; while(*p == ' ') p++;
        strncpy(vehicle_type, p, sizeof(vehicle_type) - 1);
        vehicle_type[sizeof(vehicle_type) - 1] = 0;
    }
    else if (strncmp(line, "Battery design voltage:", 23) == 0) {
        battery_design_voltage = atoi(line + 23);
    }
    else if (strncmp(line, "Battery design capacity:", 24) == 0) {
        battery_design_capacity = atoi(line + 24);
    }
    else if (strncmp(line, "Battery cell type:", 18) == 0) {
        char* p = line + 18; while(*p == ' ') p++;
        strncpy(battery_cell_type, p, sizeof(battery_cell_type) - 1);
        battery_cell_type[sizeof(battery_cell_type) - 1] = 0;
    }
    else if (strncmp(line, "BMS Current:", 12) == 0) {
        bms_current = atof(line + 12);
    }
    else if (strncmp(line, "BMS SOC:", 8) == 0) {
        bms_soc = atoi(line + 8);
    }
    else if (strncmp(line, "BMS Max Temp:", 13) == 0) {
        bms_max_temp = atoi(line + 13);
    }
    else if (strncmp(line, "BMS Min Temp:", 13) == 0) {
        bms_min_temp = atoi(line + 13);
    }
    else if (strncmp(line, "BMS Voltage:", 12) == 0) {
        bms_voltage = atof(line + 12);
    }
    else if (strncmp(line, "BMS charge status:", 18) == 0) {
        bms_charge_status = atoi(line + 18);
    }
    else if (strncmp(line, "BMS max cell diff:", 18) == 0) {
        bms_max_cell_diff = atoi(line + 18);
    }
    else if (strncmp(line, "CTRL current:", 13) == 0) {
        ctrl_current = atoi(line + 13);
    }
    else if (strncmp(line, "CTRL voltage:", 13) == 0) {
        ctrl_voltage = atoi(line + 13);
    }
    else if (strncmp(line, "CTRL throttle:", 14) == 0) {
        ctrl_throttle = atoi(line + 14);
    }
    else if (strncmp(line, "CTRL temp:", 10) == 0) {
        ctrl_temp = atoi(line + 10);
    }
    else if (strncmp(line, "CTRL motor temp:", 16) == 0) {
        ctrl_motor_temp = atoi(line + 16);
    }
    else if (strncmp(line, "OCV_state:", 10) == 0) {
        ocv_state = atoi(line + 10);
    }
    else if (strncmp(line, "VCU SOC:", 8) == 0) {
        vcu_soc = atoi(line + 8);
    } 
    else if (strncmp(line, "VCU Voltage:", 12) == 0) {
        vcu_voltage = atof(line + 12);
    }
    else if (strncmp(line, "VCU SOC Voltage:", 16) == 0) {
        vcu_soc_voltage = atoi(line + 16);
    }
    else if (strncmp(line, "VCU Throttle Voltage:", 21) == 0) {
        vcu_throttle_voltage = atoi(line + 21);
    }
    else if (strncmp(line, "Engine State:", 13) == 0) {
        char* p = line + 13; while(*p == ' ') p++;
        strncpy(engine_state, p, sizeof(engine_state) - 1);
        engine_state[sizeof(engine_state) - 1] = 0;
        if (strcmp(engine_state, "ENGINE_STATE_OFF") == 0) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
        } else {
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
        }
    }
    else if (strncmp(line, "outDCDC_EN:", 11) == 0) {
        out_dcdc_en = atoi(line + 11);
    }
    else if (strncmp(line, "outDischarge:", 13) == 0) {
        out_discharge = atoi(line + 13);
    }
    else if (strncmp(line, "outMCULimp:", 11) == 0) {
        out_mcu_limp = atoi(line + 11);
    }
    else if (strncmp(line, "outCTRL_EN:", 11) == 0) {
        out_ctrl_en = atoi(line + 11);
    }
    else if (strncmp(line, "outCTRL_Reverse:", 16) == 0) {
        out_ctrl_reverse = atoi(line + 16);
    }
    else if (strncmp(line, "outBrake:", 9) == 0) {
        out_brake = atoi(line + 9);
    }
    else if (strncmp(line, "outMosfetChargePort:", 20) == 0) {
        out_mosfet_charge_port = atoi(line + 20);
    }
    else if (strncmp(line, "outBoot0:", 9) == 0) {
        out_boot0 = atoi(line + 9);
    }
    else if (strncmp(line, "outThrottle:", 12) == 0) {
        out_throttle = atoi(line + 12);
    }
    else if (strncmp(line, "powerLimitSOC:", 14) == 0) {
        power_limit_soc = atoi(line + 14);
    }
    else if (strncmp(line, "powerLimitTemp:", 15) == 0) {
        power_limit_temp = atoi(line + 15);
    }
    else if (strncmp(line, "maxDischargeCurrent:", 20) == 0) {
        max_discharge_current = atoi(line + 20);
    }
    else if (strncmp(line, "maxRechargeCurrent:", 19) == 0) {
        max_recharge_current = atoi(line + 19);
    }
    else if (strncmp(line, "voltageGood:", 12) == 0) {
        voltage_good = atoi(line + 12);
    }
    else if (strncmp(line, "voltageStandby:", 15) == 0) {
        voltage_standby = atoi(line + 15);
    }
    else if (strncmp(line, "voltageLow:", 11) == 0) {
        voltage_low = atoi(line + 11);
    }
    else if (strncmp(line, "speed:", 6) == 0) {
        vcu_speed = atoi(line + 6);
    }
    else if (strncmp(line, "consumption:", 12) == 0) {
        consumption = atoi(line + 12);
    }
    else if (strncmp(line, "remainingDistance:", 18) == 0) {
        remaining_distance = atoi(line + 18);
    }
    else if (strncmp(line, "##############################", 30) == 0) {
        // Blockende erkannt, Display aktualisieren
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t interval = (current_display_mode == 1) ? 100 : 500;
        if (now - last_display_update >= interval) {
            update_vehicle_display();
            last_display_update = now;
        }
    }
}

void setup() {

        // Button Initialisierung
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // Onboard-LED initialisieren und einschalten, um den Betrieb zu signalisieren
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); // LED aus (wird später durch Engine State gesteuert) (funktioniert noch nicht!)

    // GPS UART Initialisierung (UART0)
    // GP16 = UART0 TX (an GPS RXD), GP17 = UART0 RX (an GPS TXD)
    const uint GPS_TX_PIN = 16;
    const uint GPS_RX_PIN = 17;
    uart_init(uart0, 115200);
    gpio_set_function(GPS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(GPS_RX_PIN, GPIO_FUNC_UART);

    // I2C Initialisierung (SDA=GP4, SCL=GP5)
    i2c_init(i2c0, 100 * 1000); // Reduziert auf 100 kHz für längere Kabel
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);

    // OLED Initialisierung
    u8g2_Setup_ssd1306_i2c_128x32_univision_f(&u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetContrast(&u8g2, 255);
    //set_display_invert(true); // Invertiert die Displayfarben
    u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
    
    oled_status("Host Start");
    
    // Initialisiert den TinyUSB Host Stack
    if (!tusb_init()) {
        oled_status("Init Fehler!");
    }
}

int main() {
    setup();
    
    static bool last_btn_state = true;
    static uint32_t btn_press_start = 0;
    static bool long_press_handled = false;
    
    while (1) {
        tuh_task(); // TinyUSB Host Task (muss ständig laufen)

        // Button Abfrage
        bool btn_state = gpio_get(BUTTON_PIN);
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (!btn_state && last_btn_state) {
            // Taste gedrückt (Falling Edge)
            btn_press_start = now;
            long_press_handled = false;
        } else if (!btn_state && !last_btn_state) {
            // Taste gehalten
            if (!long_press_handled && (now - btn_press_start > 2000)) { // 2 Sekunden für Bootloader
                long_press_handled = true;
                oled_status("Bootloader...");
                sleep_ms(100); // Zeit für Display-Update geben
                reset_usb_boot(0, 0);
            }
        } else if (btn_state && !last_btn_state) {
            // Taste losgelassen (Rising Edge) - Kurzer Druck
            if (!long_press_handled && (now - btn_press_start > 50)) { // Entprellung
                current_display_mode++;
                if (current_display_mode > 2) current_display_mode = 0;
                update_vehicle_display();
            }
        }
        last_btn_state = btn_state;

        // GPS Daten lesen (UART0)
        while (uart_is_readable(uart0)) {
            uint8_t ch = uart_getc(uart0);
            if (ch == '\n' || ch == '\r') {
                if (gps_line_pos > 0) {
                    gps_line_buf[gps_line_pos] = 0;
                    process_gps_line(gps_line_buf);
                    gps_line_pos = 0;
                }
            } else if (gps_line_pos < sizeof(gps_line_buf) - 1) {
                gps_line_buf[gps_line_pos++] = ch;
            }
        }
    }
    return 0;
}

// --- TinyUSB Callbacks ---

// Wird aufgerufen, wenn ein Gerät angeschlossen wird
void tuh_mount_cb(uint8_t dev_addr) {
    char tmp[32];
    sprintf(tmp, "Verbund.: %d", dev_addr);
    oled_status(tmp);
}

// Wird aufgerufen, wenn ein Gerät entfernt wird
void tuh_umount_cb(uint8_t dev_addr) {
    char tmp[32];
    sprintf(tmp, "Getrennt: %d", dev_addr);
    oled_status(tmp);
}

// Wird aufgerufen, wenn ein CDC (Serial) Interface erkannt wird
void tuh_cdc_mount_cb(uint8_t idx) {
    // WICHTIG: DTR setzen, damit das CDC-Gerät sendet (z.B. Arduino/ESP)
    tuh_cdc_set_control_line_state(idx, 0x03, NULL, 0); // DTR | RTS
    oled_status("CDC Ready");
}

// Wird aufgerufen, wenn Daten empfangen wurden
void tuh_cdc_rx_cb(uint8_t idx) {
    // Daten direkt lesen, tuh_cdc_read gibt die Anzahl der gelesenen Bytes zurück
    uint32_t read_bytes = tuh_cdc_read(idx, buf, sizeof(buf) - 1);

    for (uint32_t i = 0; i < read_bytes; i++) {
        char c = buf[i];
        // Zeilenende erkennen (CR oder LF)
        if (c == '\n' || c == '\r') {
            if (rx_line_pos > 0) {
                rx_line_buf[rx_line_pos] = 0; // Null-Terminierung
                process_line(rx_line_buf);
                rx_line_pos = 0;
            }
        } else {
            // Zeichen speichern, wenn Puffer nicht voll
            if (rx_line_pos < sizeof(rx_line_buf) - 1) {
                rx_line_buf[rx_line_pos++] = c;
            }
        }
    }
}
