/* ************ZWUtil.c***************************************************
 * ZWUtil provides a very minimal example utility to interface to the Z-Wave
 * SerialAPI to read serialAPI information from the device.
 * USAGE: ZWUtil <uart port>
 */

#include <stdio.h>
#include <fcntl.h>      /* File Control Definition */
#include <termios.h>    /* Terminal Control Definition */
#include <unistd.h>     /* read/write definition */
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>
#include "ZWave.h"

// RX/TX UART buffer size. Most Z-Wave frames are under 64 bytes.
#define BUF_SIZE 128

#define RETRY_CNT 3 //retry 3 times

#define OPTSTRING "hvu:"

#define LONG_OPT_VERSION 0
#define LONG_OPT_INFO 1
#define LONG_OPT_CMD 2
#define LONG_OPT_HELP 3

#define OPTIONS \
  "\n%s\nOPTIONS\n" \
  " -h        Print this help message.\n" \
  " -v        Print the software version defined in the application.\n" \
  " -u <uart port, e.g. /dev/ttyACM0>\n"  \
  " --info    Query the SerialAPI device and provide details to the console.\n" \
  " --cmd <ASCII hex command string> Allows verification/running Z-Wave commands.\n" \


static struct option long_options[] = {
     {"version",    no_argument,       0,  LONG_OPT_VERSION },
     {"info",       no_argument, 0,  LONG_OPT_INFO },
     {"cmd",        required_argument, 0,  LONG_OPT_CMD },
     {"help",       no_argument, 0,  LONG_OPT_HELP },
     {0,           0,                 0,  0  }};

#define VERSION_MAJ	1u
#define VERSION_MIN	0u

int serial;
char readBuf[BUF_SIZE];
char sendBuf[BUF_SIZE];

char portstring[BUF_SIZE]; //use same buffer size for portstring

static enum app_states {
  app_state_info,
  app_state_cmd
} app_state = app_state_info;

#define MAX_CMD_STRING_LEN  16u
uint8_t cmd_data[MAX_CMD_STRING_LEN/2]; //half of string length due to ascii hex data in string
uint8_t cmd_len; //how many bytes in cmd_data

void usage(char* appname) {
    printf(OPTIONS, appname);
}

unsigned int toInt(char c) {
  /* Convert ASCII hex to binary, return -1 if error */
  if (c >= '0' && c <= '9') {
    return      c - '0';
  } else if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  } else if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  } else {
    return -1; //error
  }

}

unsigned char calc_checksum(char *pkt, int len) { /* returns the checksum of PKT */
    int i;
    int sum=0xff;
    for(i=0;i<len;i++) {
        sum ^= *(pkt+i);
    }
    return(sum&0x0FF);
}

int GetSerial(char *pkt) { /* Get SerialAPI frame from UART. Returns the length of pkt and data in PKT */
    /* strips the SOF/LEN/Type and checksum and ACKs the frame if the checksum is OK */
    int i,j,len,type,index;
    char ack=ACK;
    int searching=true;
    i=0;
    unsigned char checksum=0xff;

    while (searching) {                     // Seach for the SOF is complete when we find the SOF and type fields
        for (j=0; i<1 && j<250; j++) {      // wait up to about 250ms - if waiting for a routed frame this might need to be longer
            i=read(serial,pkt,1);
            if (i==1 && readBuf[0]!=SOF) i=0;  // drop anything until SOF
            usleep(1000);                   // Wait 1ms
        }
        if (i!=1) {
            return(-1);   // no frame so just return -1
        }
        i=read(serial,pkt,1);                  // LEN (includes LEN and TYPE)
        len=pkt[0];
        i=read(serial,pkt,1);                  // TYPE (must be 00 or 01)
        type=pkt[0];
        if (type<=0x01) searching=false;    // TODO could also qualify that LEN is less than 64?
    }
    i=0; index=0;
    for (j=0; j<1000 && index<(len-1); j++) {
        i=read(serial,&pkt[index],len);
        if (i<1) {                          // Nothing yet
            usleep(100);                    // Wait 100uS
        }
        if (i>0) {
            index+=i;
        }
    }
    // Add the len and type at the end of the array for checksum calculation
    // purposes ONLY and calculate. Should be zero if good.
    pkt[index++] = len;
    pkt[index++] = type;
    checksum = calc_checksum(pkt, len+1); //add one for the checksum byte
    if (checksum != 0) {
      printf("checksum mismatch on received serial packet! Exiting.\r\n");
      close(serial);
      exit(-1);
    }
    write(serial,&ack,1);   // ACK the frame
    return(len-2);      // remove LEN and TYPE
} /* GetSerial */

int SendSerial(const char *pkt,int len) { /* send SerialAPI command PKT of length LEN after encapsulating */
    /* Returns ACK/NAK/CAN if the packet was delivered to the serial, -1 if no response */
    /* if not ACKed, will try a total of 3 times */
    /* SerialAPI format:
     * SOF      =0x01
     * LEN      =Number of bytes in this frame not including SOF and CHECKSUM (or all bytes-2)
     * Type     =0x00=Request, 0x01=Response
     * FUNCID   =SerialAPI command byte
     * PARAMS(n)=Command parameters and/or data bytes
     * CHECKSUM = XOR of all bytes except SOF. Should be 0 if checksum is OK. NAK is sent if checksum fails.
     */
    char buf[BUF_SIZE];
    int i;
    int retry;
    int ack=ACK;
    int sendStatus;
    buf[0]=SOF;
    buf[1]=len+2; // add LEN, TYPE
    buf[2]=REQUEST;
    memcpy(&buf[3],pkt,len);
    buf[len+3]=calc_checksum(&buf[1],len+2);
#ifdef DEBUG // tx debug
    printf("Sending");
    for (i=0;i<(len+4); i++) {
        printf(" %02X",buf[i]);
    }
    printf("\n");
#endif
    for (retry=0;retry<RETRY_CNT;retry++) {    // retry up to 3 times
        tcflush(serial,TCIFLUSH);  // purge the UART Rx Buffer
        write(serial,buf,len+4);   // Send the frame to the serial
        sendStatus=0;
        usleep(10000); // wait 10ms then read the ACK
        sendStatus = read(serial,readBuf,1);          // Get the ACK/NAK/CAN
        if (sendStatus==1) {
            if (readBuf[0]==ACK) break; // Got the ACK so break
            write(serial,&ack,1);          // Got something else so try sending an ACK to clear
        }
        sleep(2);      // wait a bit and try again
    }
    if (sendStatus!=1) {
        printf("UART Timeout\r\n");
        return(-1);
    }
    return(readBuf[0]);
} /* SendSerial */

const char* statusString(int status) {
  if (status == -1) {
    return "NO_RESPONSE";
  } else if (status == ACK) {
    return "NAK";
  } else if (status == CAN) {
    return "CAN";
  } else {
    return "UNKNOWN";
  }
}

int main(int argc, char *argv[]) { /*****************MAIN*********************/
    struct termios Settings;

    int ack,len,i,j;
    int opt;
    int option_index=0;
    uint8_t cmd_string_len=0;
    int8_t upper_nib;
    int8_t lower_nib;
    uint8_t retry_cnt;

    // Process command line options.
  while ((opt = getopt_long(argc, argv, OPTSTRING, long_options, &option_index)) != -1) {
    switch (opt) {
      // Print help.
      case 'h':
      case LONG_OPT_HELP:
        printf(OPTIONS, argv[0]);
        exit(EXIT_SUCCESS);

      case 'v':
      case LONG_OPT_VERSION:
        printf("%s version %d.%d\n",argv[0],VERSION_MAJ,VERSION_MIN);
        break;

      case LONG_OPT_INFO:
        app_state = app_state_info;
        break;

      case LONG_OPT_CMD:
        app_state = app_state_cmd;

        /* Send a custom command and print the response */
        cmd_string_len = strlen(optarg);
        cmd_len = cmd_string_len/2;
        if (cmd_string_len > MAX_CMD_STRING_LEN)
        {
          printf("String too long in -b argument: string length %zu, max = %d\n",strlen(optarg),MAX_CMD_STRING_LEN);
          exit(EXIT_FAILURE);
        }

        for (i=0; i != cmd_len; i++) {
          /* Convert arg string (e.g. "040101") to binary data */
          upper_nib = toInt(optarg[2*i]);
          lower_nib = toInt(optarg[2*i+1]);
          if (lower_nib < 0 || upper_nib < 0) {
            /* Problem with conversion - print error and exit */
            printf("Error! \"%s\" is an invalid ascii hex string. The characters need to be A-F, a-f, or 0-9.\n",optarg);
            exit(EXIT_FAILURE);
          } else {
            cmd_data[i] = (uint8_t) (16*upper_nib) + lower_nib;
          }
        }
        app_state = app_state_cmd;
        break;

        case 'u':
        memcpy(portstring, optarg, strlen(optarg));
        break;

        default:
        break;
    }
  }

  if (app_state == app_state_info || app_state == app_state_cmd) {
    if (strlen(portstring) == 0) {
      printf("UART port not specified. Must specify UART port with -u\r\n");
      exit(EXIT_FAILURE);
    }
    /* setup the UART to 115200, 8 bits, no parity, 2 stop bit. */
    serial = open(portstring,O_RDWR | O_NOCTTY | O_NONBLOCK); /* serial plugged into a Linux machine is normally at /dev/ttyACM0 */
    if (serial<0) {
      printf("Error opening %s - %s(%d)\r\n",portstring, strerror(errno), errno);
      exit(-1);
    }
    tcgetattr(serial,&Settings);
    //cfsetispeed(&Settings,B115200);
    cfsetspeed(&Settings,B115200);

    // Set raw input (non-canonical) mode.
    cfmakeraw(&Settings);

    // Ignore modem control lines.
    Settings.c_cflag |= CLOCAL;
    // Enable receiver.
    Settings.c_cflag |= CREAD;
    Settings.c_cflag &= ~PARENB;
    Settings.c_cflag &= ~CSTOPB;
    Settings.c_cflag &= ~CSIZE;
    Settings.c_cflag |= CS8;
    Settings.c_cflag &= ~CRTSCTS;
    Settings.c_iflag &= ~(IXON | IXOFF | IXANY);
    Settings.c_iflag |= IGNBRK;
    Settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    Settings.c_oflag &= ~OPOST;
    Settings.c_cc[VMIN] = BUF_SIZE;
    Settings.c_cc[VTIME] = 1; // wait this many 100mSec if no chars are available yet - need to wait for the ACK
    if (tcsetattr(serial,TCSANOW,&Settings)!=0) {
        printf("Unable to set serial attributes\r\n");
        return(-2);
    }
  }
  if (app_state == app_state_info) {
    printf("Querying SerialAPI device on %s...\r\n", portstring);
    sendBuf[0]=FUNC_ID_SERIAL_API_GET_INIT_DATA;
    retry_cnt = 0;
    do  {
      ack=SendSerial(sendBuf,1);
      #ifdef DEBUG
      if (ack == NAK) {
          printf("NAK received - retry #%d\r\n", retry_cnt);
      }
      #endif
    } while (ack == NAK && retry_cnt++ < RETRY_CNT);

    if (ack!=ACK) {
        printf("Unable to send Z-Wave SerialAPI command API_GET_INIT_DATA" \
        " (%s)\r\n",statusString(ack));
    } else {
        len=GetSerial(readBuf);
        if (len<20 || readBuf[0]!=FUNC_ID_SERIAL_API_GET_INIT_DATA) {
            printf("Incorrect response = %02X, %02X\r\n",len,readBuf[0]);
            printf("%02X, %02X\r\n",readBuf[1],readBuf[2]);
        } else {
            printf("SerialAPI protocol version=%d\r\n",readBuf[1]);
            if (readBuf[2]&0x04) printf("Secondary ");
            else                 printf("Primary ");
            if (readBuf[2]&0x01) printf("Slave ");
            else                 printf("Controller ");
            if (readBuf[2]&0x08) printf("SIS ");
            else                 printf("    ");
            printf("\nNodes:");
            for (i=0;i<readBuf[3];i++) {
                for (j=0;j<8;j++) {
                    if (readBuf[i+4]&(1<<j)) {
                        printf(" %03d", i*8+j+1);
                    }
                }
            }
            printf("\r\n");
            i = readBuf[3] + 4;
            printf("chip_type=0x%x, chip_version=0x%x\r\n",
                readBuf[i], readBuf[i+1]);
        }
    }
    // Now let's get capabilities
    sendBuf[0]=FUNC_ID_SERIAL_API_GET_CAPABILITIES;
    ack=SendSerial(sendBuf,1);
    if (ack!=ACK) {
        printf("Unable to send Z-Wave SerialAPI command SERIAL_API_GET_CAPABILITIES"
                      " (%s)\r\n",statusString(ack));
    } else {
        len=GetSerial(readBuf);
        if (len<20 || readBuf[0]!=FUNC_ID_SERIAL_API_GET_CAPABILITIES ) {
            printf("Incorrect response = %02X, %02X\n",len,readBuf[0]);
            printf("%02X, %02X\n",readBuf[1],readBuf[2]);
        } else {
            printf("SerialAPI Ver=%d.%d\r\n",readBuf[1], readBuf[2]);
            printf("Product Type/Product ID: 0x%x/0x%x\r\n",
            readBuf[5] << 8 | readBuf[6],
            readBuf[7] << 8 | readBuf[8]);
        }
    }
  }// end info
  else if (app_state == app_state_cmd) {
    //TODO: Send custom/proprietary command here
    printf("Send proprietary cmd and receive response. CMD: ");
    for (i=0;i<cmd_len;i++) {
      printf("0x%02x ", cmd_data[i]);
    }
    printf("\r\n");
    printf("Querying SerialAPI device on %s...\r\n", portstring);
    retry_cnt = 0;
    do  {
      ack=SendSerial(cmd_data,cmd_len);
      #ifdef DEBUG
      if (ack == NAK) {
          printf("NAK received - retry #%d\r\n", retry_cnt);
      }
      #endif
    } while (ack == NAK && retry_cnt++ < RETRY_CNT);

    if (ack!=ACK) {
        printf("Unable to send custom SerialAPI command " \
        " (%s)\r\n",statusString(ack));
    } else {
      len=GetSerial(readBuf);
      for (i=1;i<len;i++) {
          printf("0x%02x ", readBuf[i]);   
      }
      printf("\r\n");
    }
  }
    close(serial);
}   /* Main */
