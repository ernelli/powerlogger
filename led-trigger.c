#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#ifndef TEST_HTTP
#include <wiringPi.h>
#endif

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netdb.h>


typedef long long timestamp;

timestamp getCurrentTime() {
  struct timeval time;
  gettimeofday(&time, NULL);
  
  return (timestamp)time.tv_sec*1000 + (timestamp)time.tv_usec/1000;
}


typedef long long timestamp_us;

timestamp_us getCurrentTime_us() {
  struct timeval time;
  gettimeofday(&time, NULL);
  
  return (timestamp_us)time.tv_sec*1000000 + (timestamp_us)time.tv_usec;
}


int http_client(const char *url, const char *method, unsigned char *entity_body, int len, const unsigned char *post_body, int post_len, const char *headers) {
  char scheme[32];
  char hostname[64];
  char path[256];
  int port = 80;
  int status_code = -1;
  FILE *fp = NULL;
  int s = -1;

  struct timespec start, end;

  if(sscanf(url, "%[^:]://", scheme) != 1) {
    return -1;
  }

  //printf("scheme: %s\n", scheme);

  // skip scheme://
  url += strlen(scheme) + 3;
  
  // port given
  if(strchr(url, ':')) {
    if(sscanf(url, "%[^:]:%d%s", hostname, &port, path) != 3) {
      return -1;
    }
    //printf("hostname: %s\nport: %d\npath: %s\n", hostname, port, path);
  } else {
    if(sscanf(url, "%[^/]%s", hostname, path) != 2) {
      return -1;
    }
    //printf("hostname: %s\nport: %d\npath: %s\n", hostname, port, path);
  }

  struct hostent *hostent = gethostbyname(hostname);

  if(!hostent) {
    goto cleanup;
  }

  //printf("addr_type: %d, AF_INET: %d\n", hostent->h_addrtype, AF_INET);
  //hexdump(hostent->h_addr_list[0], 16);
  //printf("lookup: %08X\n", *(unsigned int *)hostent->h_addr_list[0]);

  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  memcpy(&addr.sin_addr, hostent->h_addr_list[0], sizeof(addr.sin_addr));

  s = socket(AF_INET, SOCK_STREAM, 0);

  if(s == -1) {
    goto cleanup;
  }

  //printf("connect to: %08X:%d\n", *(unsigned int *)&addr.sin_addr, port);
  
  if(connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "connect failed: %s\n", strerror(errno));
    goto cleanup;
  }

  fp = fdopen(s, "r+");

  char contentLengthHeader[40];
  contentLengthHeader[0] = '\0';

  if(post_len) {
    sprintf(contentLengthHeader, "content-length: %d\r\n", post_len);
  }

  fprintf(fp, "%s %s HTTP/1.1\r\n%shost: %s\r\n%sconnection: close\r\n\r\n", method, path, headers ? headers: "", hostname, contentLengthHeader);

  if(post_len) {
    if(fwrite(post_body, 1, post_len, fp) != post_len) {
      goto cleanup;
    }
  }

  fflush(fp);
  
  char status[4096];
  char buffer[4096];

  if(!fgets(status, sizeof(status), fp)) {
    goto cleanup;
  }

  //printf("%s", status);

  int chunked = 0;

  while(fgets(buffer, sizeof(buffer), fp)) {
    //printf("header: %s", buffer);

    if(strcmp(buffer, "\r\n") == 0) {
      break;
    }

    if(strcasecmp(buffer, "transfer-encoding: chunked\r\n") == 0) {
      chunked = 1;
    }
  }

  // read entity body

  char *d = entity_body;
  int chunk_size;

  if(chunked) {

    do {
      //fgets(buffer, sizeof(buffer), fp)

      if(fscanf(fp, "%x\r\n", &chunk_size) != 1) {
        break;
      }
      
      //printf("read chunk_size: %d\n", chunk_size);

      if(chunk_size >= len) {
        chunk_size = len - 1;
      }


      if(fread(entity_body, 1, chunk_size, fp) != chunk_size) {
        goto cleanup;
      }

      entity_body += chunk_size;

      len -= chunk_size;

      if(fscanf(fp, "\r\n") == EOF) {
        goto cleanup;
      }

    } while(len && chunk_size);
    
  } else {
    entity_body += fread(entity_body, 1, len, fp);
  }

  *entity_body = '\0';

  if(sscanf(status, "HTTP/%*d.%*d %d", &status_code) != 1) {
    status_code = -1;
  }

 cleanup:
  if(fp) {
    fclose(fp);
  } else if(s != -1) {
    close(s);
  }

  return status_code;
}



timestamp_us lastTrigger = 0;

int power = 0;
int kwh = 0;

int num_power = 0;
int sum_power = 0;

pthread_mutex_t power_stat_mutex = PTHREAD_MUTEX_INITIALIZER;

timestamp_us triggerStart;

int pulseStat[30];

void edge_trigger_cb() {
  //unsigned int delta = 0, now;
  timestamp_us delta = 0, now, pulseWidth;

  now = getCurrentTime_us(); //millis();

  if(digitalRead(0) == 0) {
    triggerStart = now;
  } else {
    pulseWidth = now -triggerStart;
  }  

  if(pulseWidth < 1000 || pulseWidth > 3000) {
    printf("invalid meter pulse duration: %Ld us\n", pulseWidth);
  } else {
    int bucket = pulseWidth / 100;
    if(bucket > 0 && bucket < 30) {
      pulseStat[bucket]++;
    }
  }


  if(lastTrigger) {
    delta = triggerStart - lastTrigger;
  } else {
    return;
  }

  if(delta && delta < 5000) {
    return; // ignore spurious pulses
  }

  if(delta) {
    power = (int)(3600LL*1000000LL/delta);
  } else {
    power = 0;
  }
  
  lastTrigger = triggerStart;

  pthread_mutex_lock(&power_stat_mutex) ;
  kwh++;

  power = (int)(3600LL*1000000LL/delta);
  
  sum_power += power;
  num_power++;
  
  pthread_mutex_unlock(&power_stat_mutex) ;

  // if repored power exceeds 40kW, a weird power reading has been made
  if(power > 40000) {
    printf("timestamp: %Ld, Power reading out of range: %.3f, pulse interval: %Ld us\n", triggerStart/1000, (double)power/1000, delta);
  }

  //printf("timestamp: %Ld, power: %d\n", now/1000, power);
}




int main (int argc, char *argv[])
{
#ifdef TEST_HTTP
  if(argc >= 2) {
    char sendbuffer[4096];
    char response[4096];
    memset(sendbuffer, 0, sizeof(sendbuffer));
    fread(sendbuffer, 1, sizeof(sendbuffer), stdin);
    printf("sending: %s\n", sendbuffer);
    int statusCode = http_client(argv[1], "POST", response, sizeof(response), sendbuffer, strlen(sendbuffer), "content-type: application/json\r\n");
    if(statusCode == -1) {
      printf("Failed: %s\n", strerror(errno));
    } else {
      printf("got server response, code: %d\n%s", statusCode, response);
    }
  }

#else
  printf("calling wiringPi setup\n");
  wiringPiSetup () ;

  piHiPri(99);

  pinMode (0, INPUT) ;
  pullUpDnControl (0, PUD_UP);
  
  printf("setup edge ISR\n");
  if(wiringPiISR (0, INT_EDGE_BOTH,  edge_trigger_cb) != 0) {
    printf("Failed to setup ISR");
    return 1;
  } 

  const char *http_url = NULL;

  if(argc >= 2) {
    http_url = argv[1];
  }

  printf("edge trigger setup, waiting for events\n");

  timestamp ts_now = getCurrentTime();

  while(1) {
    
    timestamp ts_done = ts_now % 10000LL;
    timestamp ts_remain = 10000LL - ts_done;
    
    delay((unsigned int)ts_remain);
    ts_now = getCurrentTime();
    int avg_power;
    double  total_power;
    pthread_mutex_lock(&power_stat_mutex) ;
    avg_power = sum_power / (num_power ? num_power : 1);
    total_power = (double)kwh / 1000;
    sum_power = 0;
    num_power = 0;
    pthread_mutex_unlock(&power_stat_mutex) ;

    char data[1024];
    sprintf(data, "{\"timestamp\":%Ld, \"kW\":%.3f, \"kWh\": %.3f }", ts_now, (double)avg_power/1000, total_power);
    //puts(data);
    if(http_url) {
      char response[2048];
      response[0] = '\0';
      int statusCode = http_client(http_url, "POST", response, sizeof(response), data, strlen(data), "content-type: application/json\r\n");
      if(statusCode != 200) {
        if(statusCode == -1) {
          fprintf(stderr, "Failed to send data, error: %s\n", strerror(errno));
        } else {
          fprintf(stderr, "Failed to send data, statusCode: %d, response: %s\n", statusCode, response);
          
        }
      }
    }
    if(!http_url) {
      printf("10 seconds passed, kWh: %.3f, power: %d\n", total_power, avg_power);
    }
  }

  return 0 ;
#endif
}
