#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "nmea_parser.h"

static gps_data_t gps;

static double nmea_to_deg(char *val, char hemi)
{
  if (!val || !val[0])
    return 0;

  double raw = atof(val);
  int deg = (int)(raw / 100);
  double min = raw - deg * 100;

  double result = deg + min / 60.0;

  if (hemi == 'S' || hemi == 'W')
    result = -result;

  return result;
}

static void parse_rmc(char *line)
{
  char *tok;
  int field = 0;

  char lat[16] = {0};
  char lon[16] = {0};
  char latH = 'N';
  char lonH = 'E';

  tok = strtok(line, ",");

  while (tok)
  {
    switch (field)
    {
    case 1:
      strncpy(gps.utc_time, tok, sizeof(gps.utc_time));
      break;

    case 2:
      gps.fix = (tok[0] == 'A');
      break;

    case 3:
      strncpy(lat, tok, sizeof(lat));
      break;

    case 4:
      latH = tok[0];
      break;

    case 5:
      strncpy(lon, tok, sizeof(lon));
      break;

    case 6:
      lonH = tok[0];
      break;

    case 7:
      gps.speed_knots = atof(tok);
      break;

    case 9:
      strncpy(gps.utc_date, tok, sizeof(gps.utc_date));
      break;
    }

    tok = strtok(NULL, ",");
    field++;
  }

  gps.latitude = nmea_to_deg(lat, latH);
  gps.longitude = nmea_to_deg(lon, lonH);
}

static void parse_gga(char *line)
{
  char *tok;
  int field = 0;

  tok = strtok(line, ",");

  while (tok)
  {
    switch (field)
    {
    case 6:
      gps.fix = atoi(tok);
      break;

    case 7:
      gps.satellites = atoi(tok);
      break;

    case 9:
      gps.altitude = atof(tok);
      break;
    }

    tok = strtok(NULL, ",");
    field++;
  }
}

void nmea_parse_line(char *line)
{
  if (!line || line[0] != '$')
    return;

  if (strstr(line, "RMC"))
    parse_rmc(line);
  else if (strstr(line, "GGA"))
    parse_gga(line);
}

gps_data_t *gps_get_data(void)
{
  return &gps;
}

void gps_update_system_time(gps_data_t *g)
{
  if (!g->fix)
    return;
  if (strlen(g->utc_time) < 6 || strlen(g->utc_date) < 6)
    return;

  struct tm t = {0};

  /* hhmmss */
  sscanf(g->utc_time, "%2d%2d%2d",
         &t.tm_hour,
         &t.tm_min,
         &t.tm_sec);

  /* ddmmyy */
  int day, mon, year;
  sscanf(g->utc_date, "%2d%2d%2d",
         &day, &mon, &year);

  t.tm_mday = day;
  t.tm_mon = mon - 1;
  t.tm_year = year + 100; // 2000+

  time_t epoch = mktime(&t);

  struct timeval now = {
      .tv_sec = epoch,
      .tv_usec = 0};

  settimeofday(&now, NULL);
}
