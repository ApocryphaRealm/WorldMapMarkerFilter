#!/usr/bin/env python3
"""Writes dist/Interface/Translations/MapMarkerList_<language>.txt (UTF-16LE with BOM) from the table below, so the eleven files
cannot drift apart. Run it after adding a string: py -3 tools/make_translations.py"""
import pathlib

LANGS = ["english", "chinese", "czech", "french", "german", "italian", "japanese", "korean", "polish", "russian", "spanish"]

T = {
"MML_PanelTitle": ["Map Markers", "地图标记", "Značky na mapě", "Marqueurs de carte", "Kartenmarkierungen", "Segnalini della mappa", "マップマーカー", "지도 표시", "Znaczniki mapy", "Метки карты", "Marcadores del mapa"],
"MML_SearchHint": ["Search by name", "按名称搜索", "Hledat podle názvu", "Rechercher par nom", "Nach Namen suchen", "Cerca per nome", "名前で検索", "이름으로 검색", "Szukaj według nazwy", "Поиск по названию", "Buscar por nombre"],
"MML_NoMatches": ["No markers match.", "没有匹配的标记。", "Žádná značka neodpovídá.", "Aucun marqueur ne correspond.", "Keine Markierung passt.", "Nessun segnalino corrisponde.", "一致するマーカーはありません。", "일치하는 표시가 없습니다.", "Brak pasujących znaczników.", "Подходящих меток нет.", "Ningún marcador coincide."],
"MML_Loading": ["Reading the map's markers...", "正在读取地图标记……", "Načítám značky mapy...", "Lecture des marqueurs de la carte...", "Kartenmarkierungen werden gelesen...", "Lettura dei segnalini della mappa...", "マップのマーカーを読み込み中...", "지도 표시를 읽는 중...", "Odczytywanie znaczników mapy...", "Чтение меток карты...", "Leyendo los marcadores del mapa..."],
"MML_NoList": ["This map does not list its markers. SkyUI's map is needed.", "此地图不提供标记列表。需要 SkyUI 的地图。", "Tato mapa neuvádí své značky. Je potřeba mapa ze SkyUI.", "Cette carte ne liste pas ses marqueurs. La carte de SkyUI est nécessaire.", "Diese Karte listet ihre Markierungen nicht auf. Die Karte von SkyUI wird benötigt.", "Questa mappa non elenca i suoi segnalini. Serve la mappa di SkyUI.", "このマップはマーカーを一覧にしません。SkyUI のマップが必要です。", "이 지도는 표시 목록을 제공하지 않습니다. SkyUI의 지도가 필요합니다.", "Ta mapa nie podaje listy znaczników. Potrzebna jest mapa ze SkyUI.", "Эта карта не даёт список меток. Нужна карта из SkyUI.", "Este mapa no enumera sus marcadores. Hace falta el mapa de SkyUI."],
"MML_HintPad": ["D-pad: move   A: centre the map   X: type   Y: delete   B: close", "方向键：移动   A：地图居中   X：输入   Y：删除   B：关闭", "D-pad: pohyb   A: vystředit mapu   X: psát   Y: smazat   B: zavřít", "Croix : déplacer   A : centrer la carte   X : saisir   Y : effacer   B : fermer", "Steuerkreuz: bewegen   A: Karte zentrieren   X: tippen   Y: löschen   B: schließen", "Croce: sposta   A: centra la mappa   X: scrivi   Y: cancella   B: chiudi", "十字キー：移動   A：マップを中央へ   X：入力   Y：削除   B：閉じる", "방향 패드: 이동   A: 지도 중앙으로   X: 입력   Y: 지우기   B: 닫기", "D-pad: ruch   A: wyśrodkuj mapę   X: pisz   Y: usuń   B: zamknij", "Крестовина: выбор   A: к метке на карте   X: ввод   Y: удалить   B: закрыть", "Cruceta: mover   A: centrar el mapa   X: escribir   Y: borrar   B: cerrar"],
"MML_HintGrid": ["D-pad: choose a letter   A: add it   Y: delete   B: back to the list", "方向键：选择字符   A：添加   Y：删除   B：返回列表", "D-pad: vybrat znak   A: přidat   Y: smazat   B: zpět na seznam", "Croix : choisir une lettre   A : l'ajouter   Y : effacer   B : retour à la liste", "Steuerkreuz: Zeichen wählen   A: hinzufügen   Y: löschen   B: zurück zur Liste", "Croce: scegli una lettera   A: aggiungi   Y: cancella   B: torna all'elenco", "十字キー：文字を選ぶ   A：追加   Y：削除   B：リストに戻る", "방향 패드: 글자 선택   A: 추가   Y: 지우기   B: 목록으로", "D-pad: wybierz znak   A: dodaj   Y: usuń   B: wróć do listy", "Крестовина: выбрать букву   A: добавить   Y: удалить   B: назад к списку", "Cruceta: elegir letra   A: añadir   Y: borrar   B: volver a la lista"],
"MML_HintKeys": ["Type to search   Arrows or wheel: move   Enter or click: centre the map   Esc: close", "输入以搜索   方向键或滚轮：移动   回车或单击：地图居中   Esc：关闭", "Pište pro hledání   Šipky nebo kolečko: pohyb   Enter nebo klik: vystředit mapu   Esc: zavřít", "Tapez pour chercher   Flèches ou molette : déplacer   Entrée ou clic : centrer la carte   Échap : fermer", "Tippen zum Suchen   Pfeile oder Mausrad: bewegen   Eingabe oder Klick: Karte zentrieren   Esc: schließen", "Scrivi per cercare   Frecce o rotella: sposta   Invio o clic: centra la mappa   Esc: chiudi", "入力して検索   矢印キーまたはホイール：移動   Enter またはクリック：マップを中央へ   Esc：閉じる", "입력하여 검색   화살표 또는 휠: 이동   Enter 또는 클릭: 지도 중앙으로   Esc: 닫기", "Pisz, aby szukać   Strzałki lub kółko: ruch   Enter lub klik: wyśrodkuj mapę   Esc: zamknij", "Вводите для поиска   Стрелки или колесо: выбор   Enter или щелчок: к метке на карте   Esc: закрыть", "Escribe para buscar   Flechas o rueda: mover   Intro o clic: centrar el mapa   Esc: cerrar"],
}

root = pathlib.Path(__file__).resolve().parent.parent / "dist" / "Interface" / "Translations"
root.mkdir(parents=True, exist_ok=True)
for i, lang in enumerate(LANGS):
    lines = []
    for key, row in T.items():
        assert len(row) == 11, key
        lines.append(f"${key}\t{row[i]}")
    (root / f"MapMarkerList_{lang}.txt").write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))
print("wrote", len(LANGS), "files,", len(T), "keys")
