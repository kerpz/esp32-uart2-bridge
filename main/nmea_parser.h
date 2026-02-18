#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

typedef struct
{
  double latitude;
  double longitude;
  float speed_knots;
  float altitude;
  int satellites;
  int fix;

  char utc_time[16];
  char utc_date[16];

} gps_data_t;

void nmea_parse_line(char *line);
gps_data_t *gps_get_data(void);
void gps_update_system_time(gps_data_t *g);

#endif
