// Included inside smoke's main after its capture/check helpers. These checks
// measure the rendered scene, rather than duplicating the QML layout formulas.
const auto quick = [&](const char* name) { return qobject_cast<QQuickItem*>(find(name)); };
const auto sceneRect = [](QQuickItem* item) { return item ? item->mapRectToScene(QRectF(0,0,item->width(),item->height())) : QRectF{}; };
const auto themeObject = engine.singletonInstance<QObject*>("BottleMusic", "Theme");
const auto theme = engine.newQObject(themeObject);
const auto themeCall = [&](const char* name, const QJSValueList& values) { return theme.property(name).callWithInstance(theme,values); };
const auto contrast = [&](QColor a, QColor b) { return themeCall("contrast",{engine.toScriptValue(a),engine.toScriptValue(b)}).toNumber(); };
const auto walkItems = [](QQuickItem* parent, const std::function<void(QQuickItem*)>& visit) {
    const auto walk = [&](const auto& self,QQuickItem* item) -> void { if (!item) return; visit(item); for (auto* child:item->childItems()) self(self,child); };
    walk(walk,parent);
};
const auto barGeometryOk = [&] {
    auto* bar=quick("bravaisBar"); auto* leading=quick("bravaisBarLeading"); auto* trailing=quick("bravaisBarTrailing");
    if (!bar || !leading || !trailing) return false;
    const QRectF bounds=sceneRect(bar); QList<QRectF> controls; bool ok=bar->height()<=48;
    walkItems(bar,[&](QQuickItem* item) {
        if (!item->isVisible() || item->width()<=0 || !(item->inherits("QQuickAbstractButton") || item->inherits("QQuickTextInput") || item->objectName()=="bravaisStatus")) return;
        const auto rect=sceneRect(item); ok=ok&&bounds.adjusted(-0.5,-0.5,0.5,0.5).contains(rect);
        for (const auto& previous:controls) { const auto overlap=rect.intersected(previous); if (overlap.width()>0.5 && overlap.height()>0.5) ok=false; }
        controls.append(rect);
    });
    return ok && sceneRect(trailing).left()-sceneRect(leading).right()>=47.99;
};
const auto navigationOk = [&] {
    auto* bar=quick("bravaisBar"); auto* field=quick("bravaisField"); auto* homeItem=find("bravaisHome");
    bool ok=bar&&field&&homeItem&&homeItem->property("seamWidth").toDouble()==0&&barGeometryOk();
    for (const char* name:{"bravaisTab_recommend","bravaisTab_playlists","bravaisTab_albums","bravaisTab_mine","bravaisTab_favorites","bravaisGlobalSearch","bravaisQueue","bravaisTools","bravaisAccount","bravaisSettings","bravaisMore"}) {
        auto* item=quick(name); ok=ok&&item&&item->isVisible()&&item->height()>=32&&sceneRect(bar).contains(sceneRect(item));
    }
    return ok&&std::abs(sceneRect(field).top()-sceneRect(bar).bottom())<0.5;
};
const auto noFloatingControls = [&] {
    bool ok=true; auto* field=quick("bravaisField");
    walkItems(quick("bravaisHome"),[&](QQuickItem* item) {
        if (!item->isVisible() || !(item->inherits("QQuickAbstractButton") || item->inherits("QQuickTextInput"))) return;
        for (auto* parent=item;parent;parent=parent->parentItem()) {
            const auto name=parent->objectName();
            if (name=="bravaisBar" || name.startsWith("bravaisTile_") || name=="bravaisFocusedTile" || name=="bravaisCentreState" || parent->inherits("QQuickPopupItem")) return;
        }
        const auto overlap=sceneRect(item).intersected(sceneRect(field)); if (overlap.width()*overlap.height()>1) ok=false;
    }); return ok;
};
const auto tileTypographyOk = [&] {
    bool ok=true;int count=0;const auto viewport=sceneRect(quick("bravaisField"));
    walkItems(quick("bravaisHome"),[&](QQuickItem* tile) {
        if(!(tile->objectName().startsWith("bravaisTile_")||tile->objectName()=="bravaisFocusedTile")||!sceneRect(tile).intersects(viewport))return;
        auto* block=qobject_cast<QQuickItem*>(findItem(tile,"bravaisTileText"));auto* scrim=findItem(tile,"bravaisScrim");if(!block||!scrim){ok=false;return;}
        const auto alphaAt=[](QObject* rectangle,double t) {
            const auto value=rectangle->property("gradient");auto* gradient=value.value<QJSValue>().toQObject();if(!gradient)gradient=value.value<QObject*>();
            QQmlListReference stops(gradient,"stops");QList<QPair<double,double>> points;
            for(qsizetype i=0;i<stops.count();i++)points.append({stops.at(i)->property("position").toDouble(),stops.at(i)->property("color").value<QColor>().alphaF()});
            if(points.isEmpty())return -1.0;
            std::stable_sort(points.begin(),points.end(),[](auto a,auto b){return a.first<b.first;});
            if(t<=points.first().first)return points.first().second;
            for(qsizetype i=1;i<points.size();i++)if(t<=points[i].first){const auto a=points[i-1],b=points[i];return a.second+(b.second-a.second)*(t-a.first)/std::max(0.0001,b.first-a.first);}
            return points.last().second;
        };
        const auto rect=sceneRect(tile),text=sceneRect(block);const bool expanded=tile->property("expanded").toBool();
        double alpha=alphaAt(scrim,(text.top()-rect.top())/rect.height());
        if(expanded)if(auto* side=findItem(tile,"bravaisScrimSide"))alpha=1-(1-alpha)*(1-alphaAt(side,(text.right()-rect.left())/rect.width()));
        ok=ok&&alpha>=(expanded?0.22:0.24)-0.001;
        const auto zoom=tile->property("pixelScale").toDouble();int texts=0;
        for(auto* child:block->childItems())if(child->inherits("QQuickText")){
            const auto size=child->property("font").value<QFont>().pixelSize()*zoom;
            if(texts++==0)ok=ok&&std::abs(size-themeCall(expanded?"focusTitlePx":"tileTitlePx",{window->width(),zoom}).toNumber())<=1;
            else if(child->isVisible())ok=ok&&size>=(expanded?13:12)-0.01;
            ok=ok&&child->property("textFormat").toInt()==0; // Text.PlainText
        }
        count++;
    });return ok&&count>0;
};
const auto wallInkOk = [&](QQuickItem* parent,QColor bg) {
    bool ok=true;
    walkItems(parent,[&](QQuickItem* item) {
        if (!item->isVisible()) return;
        for (const char* property:{"tint","hoverTint"}) if (item->property(property).isValid()) ok=ok&&contrast(item->property(property).value<QColor>(),bg)>=4.5;
    }); return ok;
};
const auto queueMaterialOk = [&] {
    auto* panel=quick("queuePanelCard"); auto* search=find("queueSearch");
    if (!panel || !search) return false;
    const auto bg=themeObject->property("wallChromeSurface").value<QColor>();
    const auto pressed=themeCall("wallDownOn",{engine.toScriptValue(bg)}).toVariant().value<QColor>();
    bool ok=panel->property("color").value<QColor>()==bg && panel->property("borderColor").value<QColor>().alpha()==0 && panel->property("radius").toDouble()==16
        && search->property("color")==themeObject->property("wallChromeInk");
    walkItems(panel,[&](QQuickItem* item) {
        if (item->isVisible() && item->inherits("QQuickText") && !item->property("text").toString().isEmpty()) {
            const auto ink=item->property("color").value<QColor>(); ok=ok&&contrast(ink,bg)>=4.5&&contrast(ink,pressed)>=4.5;
        }
    }); return ok;
};
const auto mediaGeometry = [&](const char* name,bool pixels) {
    auto* button=quick(name); auto* glyph=button?qobject_cast<QQuickItem*>(findItem(button,QString::fromLatin1(name)+"_glyph")):nullptr;
    if (!button || !glyph || !button->isVisible()) return false;
    const qreal dpr=window->devicePixelRatio(); const auto b=sceneRect(button), g=sceneRect(glyph);
    auto integral=[](qreal n){return std::abs(n-std::round(n))<0.011;};
    bool ok=integral(b.width()*dpr)&&integral(b.height()*dpr)&&integral(g.width()*dpr)
        && int(std::round((b.width()-g.width())*dpr))%2==0;
    if (!pixels) return ok;
    ok=ok&&integral(b.left()*dpr)&&integral(b.top()*dpr)&&integral(g.left()*dpr)&&integral(g.top()*dpr)
        &&std::abs((g.center().y()-b.center().y())*dpr)<0.011;
    const auto kind=button->property("media").toString();
    const auto dx=theme.property("mediaStyle").property(kind).property("dx").toNumber();
    ok=ok&&std::abs((g.left()-b.left()-(b.width()-g.width())/2)*dpr-std::round(dx*g.width()*dpr/24))<0.011;
    const auto spec=theme.property("mediaStyle").property(kind);
    const auto ink=glyph->property("color").value<QColor>();
    const auto bg=button->property("primaryFill").value<QColor>();
    IconProvider provider; QSize actual;
    const QString id=spec.property("glyph").toString()+"/"+ink.name().mid(1)+"/"+spec.property("stroke").toString()+"/fill"+(spec.property("stroked").toBool()?"":"/nostroke");
    const auto reference=provider.requestImage(id,&actual,QSize(qRound(g.width()*dpr),qRound(g.height()*dpr)));
    const auto capture=window->grabWindow(); double rw=0,rx=0,ry=0,cw=0,cx=0,cy=0;
    // A fractional window extent is rounded by the render target: 850 * 1.25
    // produces 1063 rows. Map scene coordinates to that actual raster rather
    // than treating the nominal DPR as an exact screenshot transform.
    const double sx=capture.width()/double(window->width()),sy=capture.height()/double(window->height());
    capture.copy(QRect(qRound(b.x()*sx),qRound(b.y()*sy),qRound(b.width()*sx),qRound(b.height()*sy))).save(QDir(args[2]).filePath(QString::fromLatin1(name)+"-"+kind+"-pixels.png"));
    reference.save(QDir(args[2]).filePath(QString::fromLatin1(name)+"-"+kind+"-reference.png"));
    for(int y=0;y<reference.height();y++) for(int x=0;x<reference.width();x++) { const double weight=qAlpha(reference.pixel(x,y))/255.0; rw+=weight;rx+=(x+0.5)*weight;ry+=(y+0.5)*weight; }
    const int red=ink.red()-bg.red(),green=ink.green()-bg.green(),blue=ink.blue()-bg.blue();
    const int channel=std::abs(red)>=std::max(std::abs(green),std::abs(blue))?0:std::abs(green)>=std::abs(blue)?1:2;
    const double difference=channel==0?red:channel==1?green:blue;
    if (!difference || !rw) return false;
    const QPointF centre(b.center().x()*sx,b.center().y()*sy); const double radius=std::min(b.width()*sx,b.height()*sy)/2-2;
    for(int y=qFloor(b.top()*sy);y<qCeil(b.bottom()*sy);y++) for(int x=qFloor(b.left()*sx);x<qCeil(b.right()*sx);x++) {
        if (x<0||y<0||x>=capture.width()||y>=capture.height()||QLineF(QPointF(x+0.5,y+0.5),centre).length()>radius) continue;
        const QColor sample=capture.pixelColor(x,y);
        const double weight=std::clamp(((channel==0?sample.red()-bg.red():channel==1?sample.green()-bg.green():sample.blue()-bg.blue()))/difference,0.0,1.0);
        cw+=weight;cx+=(x+0.5)*weight;cy+=(y+0.5)*weight;
    }
    const double ex=cw?std::abs(cx/cw-(g.left()*sx+rx/rw*g.width()*sx/reference.width())):999, ey=cw?std::abs(cy/cw-(g.top()*sy+ry/rw*g.height()*sy/reference.height())):999;
    auto* texture=glyph->childItems().isEmpty()?nullptr:glyph->childItems().first();
    report.insert(QStringLiteral("media_")+QLatin1String(name)+"_"+kind,QJsonObject{{"pixel_error_x",ex},{"pixel_error_y",ey},{"box",QJsonArray{b.x(),b.y(),b.width(),b.height()}},{"glyph",QJsonArray{g.x(),g.y(),g.width(),g.height()}},{"dpr",dpr},{"render_scale",QJsonArray{sx,sy}},
        {"image",texture?QJsonArray{texture->property("sourceSize").toSize().width(),texture->property("sourceSize").toSize().height(),texture->property("paintedWidth").toDouble(),texture->property("paintedHeight").toDouble()}:QJsonArray{}},{"raster_scale",glyph->property("rasterScale").toDouble()},{"image_rect",texture?QJsonArray{sceneRect(texture).x(),sceneRect(texture).y(),sceneRect(texture).width(),sceneRect(texture).height()}:QJsonArray{}}});
    return ok&&ex<=0.35&&ey<=0.35;
};
const auto playerCompositionOk = [&](QString suffix) {
    auto* cover=quick("playerCoverArt"); auto* active=quick("activeLyricText"); auto* line=quick(("lyricLine_"+QString::number(music.lyricIndex())).toUtf8()); auto* view=quick("lyricView");
    if (!cover||!active||!line||!view) { check("player_geometry_available_"+suffix,false);return; }
    const auto c=sceneRect(cover), a=sceneRect(active), row=sceneRect(line);
    const auto gap=themeCall("coverLyricGap",{c.width()}).toNumber();
    check("cover_lyric_gap_"+suffix,std::abs(a.left()-c.right()-gap)<=2);
    check("current_line_on_focus_"+suffix,std::abs(row.center().y()-view->mapToScene(QPointF(0,view->property("focusLine").toDouble())).y())<=2);
    check("current_line_aligns_cover_"+suffix,std::abs(row.center().y()-c.center().y())<=2);
    check("info_aligns_with_cover_"+suffix,std::abs(sceneRect(quick("playerTrackTitle")).left()-(c.left()-10))<=1);
    bool whole=true; const auto bounds=sceneRect(quick("lyricList"));
    walkItems(view,[&](QQuickItem* item) { if(item->objectName().startsWith("lyricLine_") && !item->property("activeLine").toBool() && item->opacity()>0.01) { const auto r=sceneRect(item); whole=whole&&r.top()>=bounds.top()-0.5&&r.bottom()<=bounds.bottom()+0.5; } });
    check("readable_mode_no_partial_lines_"+suffix,whole);
};
const auto immersiveCompositionOk = [&](QString suffix) {
    auto* cover=quick("immersiveArtwork"); auto* view=quick("immersiveLyricView"); auto* line=quick(("lyricLine_"+QString::number(music.lyricIndex())).toUtf8()); auto* page=find("immersivePlayerPage");
    if(!cover||!view||!line||!page) {check("immersive_geometry_available_"+suffix,false);return;}
    const auto c=sceneRect(cover),v=sceneRect(view); const auto composition=page->property("composition").value<QJSValue>();
    auto* list=find("immersiveLyricList");
    report.insert("immersive_"+suffix,QJsonObject{{"cover_centre_y",c.center().y()},{"row_centre_y",sceneRect(line).center().y()},{"focus",view->property("focusLine").toDouble()},{"following",view->property("followPlayback").toBool()},
        {"content_y",list?list->property("contentY").toDouble():0},{"range_mode",list?list->property("highlightRangeMode").toInt():-1},{"view_index",list?list->property("currentIndex").toInt():-1},{"audio_index",music.lyricIndex()}});
    check("immersive_art_lyric_gap_"+suffix,v.width()>=320 && std::abs(v.left()-c.right()-composition.property("g").toNumber())<=2);
    check("immersive_index_owned_by_audio_"+suffix,list&&list->property("currentIndex").toInt()==music.lyricIndex());
    check("immersive_focus_aligned_"+suffix,std::abs(sceneRect(line).center().y()-c.center().y())<=2);
};
QJsonObject screenInfo{{"geometry",QJsonArray{window->screen()->geometry().width(),window->screen()->geometry().height()}},
    {"available",QJsonArray{window->screen()->availableGeometry().width(),window->screen()->availableGeometry().height()}},{"dpr",window->devicePixelRatio()}};
report.insert("screen",screenInfo);
const auto captureSizes = [&] {
    QList<QSize> sizes; QJsonArray skipped;
    const QString requested=qEnvironmentVariable("BOTTLE_SMOKE_SIZES","1280x850,900x620,1600x1000,1400x620,1920x1080,max");
    for (const auto& value:requested.split(',')) {
        const auto dimensions=value.split('x'); const auto available=window->screen()->availableGeometry().size();
        const QSize size=value=="max"?available:dimensions.size()==2?QSize(dimensions[0].toInt(),dimensions[1].toInt()):QSize{};
        if(size.width()<900||size.height()<620||size.width()>available.width()||size.height()>available.height()) skipped.append(value); else if(!sizes.contains(size)) sizes.append(size);
    }
    report.insert("skipped_sizes",skipped); return sizes;
};
