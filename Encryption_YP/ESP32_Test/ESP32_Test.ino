void setup() {
  Serial.begin(115200);
}

void loop() {
  int seq = 102;
  float distance_m = 0.5 + (esp_random()%21)/100.0;
  float interval_ms = seq + (esp_random()%999999)/1000000.0;
  int pad_inferred_from_data = 0;

  Serial.print(seq);
  Serial.print(",");
  Serial.print(distance_m);
  Serial.print(",");
  Serial.print(interval_ms,6);
  Serial.print(",");
  Serial.println(pad_inferred_from_data);

  delay(100);
}
