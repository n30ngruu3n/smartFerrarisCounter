const char MAIN_page[] = R"=====(
<font size="3" face="tahoma" color="black">
<form action="/get">
    Umdrehungen pro kWh: <input type="text" name="setRepPerKWh" value="72">
<br><br>
    Initialer Z&auml;hlerstand: <input type="text" name="setInitialCounter" value="0">
<input type="submit" value="Werte &uuml;bernehmen">
</form>
<form action="/resetValues">
<input type="submit" value="Statistik zur&uuml;rcksetzen">
</form>
<form action="/toggleDisplay">
    Display umschalten (ON/OFF): <input type="submit" value="Toggle!">
</form>
)=====";