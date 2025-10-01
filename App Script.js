function doPost(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  var data = JSON.parse(e.postData.contents);
  sheet.appendRow([new Date(), data.temperatura, data.humedad, data.lpg_ppm, data.h2_ppm, data.humo_ppm, data.benceno_mgL, data.alcohol_mgL, 
  data.co_ppm, data.co2_ppm, data.amoniaco_ppm, data.tolueno_ppm, data.ica_valor])
}
