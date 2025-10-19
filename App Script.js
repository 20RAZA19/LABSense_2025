function doPost(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  var data = JSON.parse(e.postData.contents);
  sheet.appendRow([new Date(), data.temperatura, data.humedad, data.lpg_ppm, data.h2_ppm, data.humo_ppm, data.benceno_mgL, data.alcohol_mgL, 
  data.co_ppm, data.co2_ppm, data.amoniaco_ppm, data.tolueno_ppm, data.ica_valor])
}

function doGet(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  // Toma los datos de la última fila
  var lastRow = sheet.getLastRow();
  var lastRowData = sheet.getRange(lastRow, 1, 1, sheet.getLastColumn()).getValues()[0];

  // Asignación comandos de usuario al índice de columna y unidades correctos
  const sensorMapping = {
    "/temperatura": { index: 1, unit: "°C", name: "🌡️ Temperatura" },
    "/humedad":    { index: 2, unit: "%", name: "💧 Humedad" },
    "/lpg":        { index: 3, unit: "ppm", name: "💨 LPG" },
    "/h2":         { index: 4, unit: "ppm", name: "💨 Hidrógeno (H₂)" },
    "/humo":       { index: 5, unit: "ppm", name: "🔥 Humo" },
    "/benceno":    { index: 6, unit: "mg/L", name: "🧪 Benceno" },
    "/alcohol":    { index: 7, unit: "mg/L", name: "🍷 Alcohol" },
    "/co":         { index: 8, unit: "ppm", name: "💨 Monóxido de Carbono (CO)" },
    "/co2":        { index: 9, unit: "ppm", name: "💨 Dióxido de Carbono (CO₂)" },
    "/amoniaco":   { index: 10, unit: "ppm", name: "🧪 Amoniaco" },
    "/tolueno":    { index: 11, unit: "ppm", name: "🧪 Tolueno" },
    "/ica":        { index: 12, unit: "", name: "📊 ICA (Índice Calidad Aire)" },
    "/dht22":      { index: ['/temperatura', '/humedad'], unit: "", name: "Datos del DHT22" },
    "/mq2":      { index: ['/lpg', '/h2', '/humo'], unit: "", name: "Datos del MQ2" },
    "/mq3":      { index: ['/benceno', '/alcohol'], unit: "", name: "Datos del MQ3" },
    "/mq7":      { index: ['/co2', '/co2'], unit: "", name: "Datos del MQ7" },
    "/mq135":      { index: ['/amoniaco', '/tolueno', '/ica'], unit: "", name: "Datos del MQ135" },
    "/anemometro":      { index: ['/velocidad'], unit: "", name: "Datos del Anemómetro" }
  };

  // Obtener el comando enviado por el usuario desde Twilio (por ejemplo, "/temperatura")
  var command = e.parameter.Body.toLowerCase().trim();
  
  let messageText = "";
  // Fomatea la marca de tiempo de la hoja para que sea más legible
  const timestamp = new Date(lastRowData[0]).toLocaleString();

  // Comprobar si el comando del usuario es válido
  if (command in sensorMapping) {
    const sensor = sensorMapping[command];
    messageText = `*Última lectura de LABSense:*\n_${timestamp}_\n\n`
    if(Array.isArray(sensor.index))
    {
      messageText += `${sensor.name}:\n\n`
      sensor.index.forEach((sMap) => {
        if(sMap in sensorMapping){
          const sensorIt = sensorMapping[sMap];
          const value = lastRowData[sensorIt.index];
          messageText += `*${sensorIt.name}*: ${value} ${sensorIt.unit}\n`;
        }
      })
    }
    else {
      const value = lastRowData[sensor.index];
      // Crea la respuesta al mensaje según la solicitud
      messageText = `*${sensor.name}*: ${value} ${sensor.unit}`;
    }
  } else {
    // Crea un mensaje de ayuda si la solicitud no es válida
    messageText = "Comando no válido. 🤔\n\nPor favor, usa uno de los siguientes:\n" + Object.keys(sensorMapping).join("\n");
  }

  // Cree la respuesta XML de TwiML requerida por Twilio
  var twiml = XmlService.createElement('Response');
  var message = XmlService.createElement('Message');
  message.setText(messageText);
  twiml.addContent(message);
  
  // Regresa el XML como respuesta
  return ContentService.createTextOutput(messageText)
    .setMimeType(ContentService.MimeType.XML);
}
