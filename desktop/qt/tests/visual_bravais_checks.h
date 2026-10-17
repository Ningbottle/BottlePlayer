{
    const auto homeScript=engine.newQObject(bravais);
    const auto zeroRows=engine.toScriptValue(QVariantList{QVariantMap{{"id","zero"},{"name","Empty"},{"count",0}}});
    const auto publicZero=homeScript.property("collections").callWithInstance(homeScript,{zeroRows,false});
    const auto personalZero=homeScript.property("collections").callWithInstance(homeScript,{zeroRows,true});
    check("public_zero_count_is_unknown_personal_zero_is_valid",publicZero.property(0).property("count").toInt()==-1&&personalZero.property(0).property("count").toInt()==0);
    bool colors=true;
    for(int r:{0,64,128,192,255}) for(int g:{0,64,128,192,255}) for(int b:{0,64,128,192,255}) {
        const QColor c(r,g,b); const auto ink=themeCall("inkOn",{engine.toScriptValue(c)}).toVariant().value<QColor>();
        const auto focus=themeCall("wallFocusFor",{engine.toScriptValue(c)}).toVariant().value<QColor>();
        colors=colors&&contrast(ink,c)>=4.5&&contrast(focus,themeObject->property("wallBg").value<QColor>())>=3;
    }
    check("theme_ink_on_contrast",colors);
    bool zooms=true;
    for(const auto& size:{QSize(1280,850),QSize(900,620),QSize(1600,1000),QSize(1400,620),QSize(1365,733),QSize(0,0)}) {
        const double z=themeCall("bravaisZoom",{size.width(),size.height()}).toNumber();
        const double title=themeCall("tileTitlePx",{size.width(),z}).toNumber(),sub=themeCall("tileSubtitlePx",{size.width(),z}).toNumber();
        zooms=zooms&&std::isfinite(z)&&z>=0.45&&z<=0.76&&title>=15&&sub>=12;
    }
    check("theme_bravais_zoom_table",zooms);
    const auto packing=engine.newQObject(engine.singletonInstance<QObject*>("BottleMusic","BravaisLayout"));
    bool exact=true,rhythm=true; int samples=0;
    for(int column=-4;column<4;column++) for(int row=-4;row<4;row++) for(int slot=0;slot<12;slot++) {
        const auto at=packing.property("at").callWithInstance(packing,{column,row,slot,72});
        const auto expanded=packing.property("expand").callWithInstance(packing,{at,72});
        QList<QRect> rects; QSet<QString> sizes; int area=0;
        for(int i=0;i<12;i++) {
            const auto cell=expanded.property(i); const int w=qRound((cell.property("w").toNumber()+8)/136),h=qRound((cell.property("h").toNumber()+8)/136);
            const QRect rect(qRound(cell.property("x").toNumber()/136)-column*12,qRound(cell.property("y").toNumber()/136)-row*8,w,h);
            exact=exact&&QRect(0,0,12,8).contains(rect); area+=w*h;
            for(const auto& previous:rects) exact=exact&&!rect.intersects(previous);
            rects.append(rect); if(i==slot) exact=exact&&w==6&&h==6; else sizes.insert(QString::number(w)+"x"+QString::number(h));
        }
        exact=exact&&area==96; rhythm=rhythm&&sizes.size()>=3; samples++;
    }
    report.insert("packing_samples",samples); check("focus_reflow_exact_cover",exact); check("focus_reflow_rhythm",rhythm);
    const auto p30=packing.property("period").callWithInstance(packing,{30}),p72=packing.property("period").callWithInstance(packing,{72});
    check("period_reduces_nearby_repeats",p30.property("columns").toInt()==2&&p30.property("rows").toInt()==2&&p72.property("columns").toInt()==3&&p72.property("rows").toInt()==2);
    IconProvider provider; QSize actual;
    const auto inkMass=[](const QImage& image){double sum=0;for(int y=0;y<image.height();y++)for(int x=0;x<image.width();x++)sum+=qAlpha(image.pixel(x,y));return sum;};
    check("icon_stroke_applied",inkMass(provider.requestImage("x/ffffff/3",&actual,QSize(48,48)))>=2*inkMass(provider.requestImage("x/ffffff/1",&actual,QSize(48,48))));
    const auto pause=provider.requestImage("media-pause/ffffff/1.5/fill/nostroke",&actual,QSize(48,48));
    QList<int> widths; int run=0;
    for(int x=0;x<pause.width();x++){bool ink=false;for(int y=0;y<pause.height();y++)ink=ink||qAlpha(pause.pixel(x,y))>127;if(ink)run++;else if(run){widths.append(run);run=0;}}
    check("media_pause_matches_web",widths.size()==2&&std::abs(widths[0]-10)<=1&&std::abs(widths[1]-10)<=1);
    const auto missing=provider.requestImage("no-such-icon/ffffff/1.75",&actual,QSize(24,24));provider.requestImage("no-such-icon/ffffff/1.75",&actual,QSize(24,24));
    int expected=0; {QMutexLocker lock(&warningMutex);for(qsizetype i=warnings.size();i-->0;)if(warnings[i].startsWith("BottleMusic: missing icon no-such-icon")){expected++;warnings.removeAt(i);}}
    check("missing_icon_is_reported",inkMass(missing)==0&&expected==1);
    QQmlComponent component(&engine); component.setData("import QtQuick\nimport BottleMusic\nActionButton { objectName: \"mediaProbe\"; media: \"pause\"; text: \"暂停\" }",QUrl());
    std::unique_ptr<QObject> probe(component.create()); auto* probeGlyph=probe?findItem(qobject_cast<QQuickItem*>(probe.get()),"mediaProbe_glyph"):nullptr;
    check("text_button_pause_glyph",probeGlyph&&probeGlyph->property("name")=="media-pause"&&!probeGlyph->property("stroked").toBool()&&probeGlyph->property("filled").toBool());
    if(probe)probe->setProperty("media","play"); check("text_button_play_glyph",probeGlyph&&probeGlyph->property("name")=="media-play"&&probeGlyph->property("stroked").toBool());
}
