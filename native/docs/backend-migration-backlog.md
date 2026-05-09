# EchoMusic Native Backend Migration Backlog

## Current Backend State

Implemented:

- CMake targets for `EchoCore`, `EchoStorage`, `EchoCompatServer`, `EchoPlayback`, `EchoWin32`, and smoke tests.
- Authorization header parsing.
- Device generation and persistence.
- `/health`
- `/server/now`
- `/register/dev`
- `/search` for `type=song`, backed by the public KuGou mobile search endpoint and normalized for the current Vue `mapSearchSong()` mapper.
- `/search/hot`, backed by the public KuGou hot-search endpoint and normalized as `data.list[].keywords[]` for the current search page.
- `/search/suggest`, using the public song-search endpoint as a lightweight suggestion source and normalized as `data[].RecordDatas[]`.
- `/search` for `type=special`, `type=album`, and `type=author`, backed by public KuGou mobile search endpoints and normalized for current playlist/album/artist mappers.
- `/song/url`, backed by the KuGou mobile play-info endpoint and normalized with top-level and nested `url`/`play_url` fields for the current player store.
- `/privilege/lite`, backed by the KuGou play-info `extra` quality hashes and normalized with both `relate_goods` and `relateGoods`.
- `/search/lyric`, backed by `lyrics.kugou.com/search` and normalized with top-level and nested `candidates`/`info`.
- `/lyric`, backed by `lyrics.kugou.com/download`; first slice returns Base64-decoded LRC as `decodeContent`/`lyric`.
- `/playlist/track/all` and `/playlist/track/all/new`, backed by the public KuGou special-song endpoint and normalized with `songs`/`info`/`list`/`songlist`.
- `/playlist/tags`, backed by the public KuGou tag recommendation endpoint and normalized with `tag_name`/`son`.
- `/top/playlist`, backed by the public KuGou tag special-list endpoint and normalized with `special_list`/`info`/`list`.
- `/rank/list`, backed by the public KuGou rank-list endpoint.
- `/rank/audio`, backed by the public KuGou rank-song endpoint and normalized for the current `mapRankSong()` mapper.
- `/top/song`, first slice mapped to KuGou new-song rank `rankid=6666`.
- `/album/detail`, backed by the public KuGou album-info endpoint.
- `/album/songs`, backed by the public KuGou album-song endpoint and normalized as paged songs.
- `/artist/detail`, backed by the public KuGou singer-info endpoint.
- `/artist/audios`, backed by the public KuGou singer-song endpoint and normalized as paged songs.
- `/artist/albums`, backed by the public KuGou singer-album endpoint and normalized as paged albums.
- Route recognition for the current renderer API wrappers.
- Stable `native_not_implemented` response for unported known routes.
- WinHTTP wrapper scaffold.
- Media Foundation playback state scaffold.

Not implemented yet:

- Real upstream KuGou API request mapping for encrypted or login-only routes.
- Contract fixtures from the Node `KuGouMusicApi`.
- Login flows.
- Search default, KRC word-level lyric decode, playlist detail/recommend, comments, MV, cloud, and user APIs.
- Android-encrypted upstream calls, including the Node `/playlist/detail` implementation that posts to `pubsongs.kugou.com/v3/get_list_info`.
- Real Media Foundation media-session playback.
- Direct `IBackendFacade` typed APIs beyond placeholders.

## Migration Principles

- Use the existing Node `KuGouMusicApi` submodule as behavior oracle.
- Add tests before each route implementation.
- Keep HTTP compatibility responses shaped for the current Vue renderer.
- Keep typed DTOs for final Win32 usage separate from raw compat JSON.
- Cache only bounded data; do not retain full playlists or large response trees in memory.

## Phase 1: Contract Harness

Deliverables:

- Add a fixture directory under `native/tests/fixtures/compat`.
- Capture Node API samples for success, empty, unauthenticated, invalid params, and upstream error.
- Add JSON-path style assertions for volatile-safe comparison.

First fixtures:

- `/register/dev`
- `/search/default`
- `/search/hot`
- `/search`
- `/song/url`
- `/search/lyric`
- `/lyric`
- `/playlist/track/all`

Acceptance:

- Smoke tests still pass.
- Contract runner can compare at least one fixture against `CompatApi`.

## Phase 2: Search And Discovery

Routes:

- `/search/default`
- `/search/hot`
- `/search/suggest`
- `/search`
- `/top/song` (first slice implemented via rank `6666`)
- `/top/album`

Implementation:

- Add upstream endpoint definitions. `type=song` now uses `mobilecdn.kugou.com/api/v3/search/song`.
- Build query strings with existing frontend parameter names.
- Preserve raw response fields needed by current mappers.
- Add bounded cache for hot/default search.

Acceptance:

- Existing Electron search page can show song, album, artist, playlist results, hot words, and suggestions through `EchoCompatServer`.
- Empty keyword and no-result cases are stable.

## Phase 3: Playback Inputs

Routes:

- `/song/url` (first slice implemented; paid/copyright-limited songs can still return an empty URL with upstream error text)
- `/privilege/lite` (first slice implemented; returns quality candidate hashes from play-info `extra`)
- `/search/lyric` (first slice implemented)
- `/lyric` (first slice implemented with decoded LRC; KRC word-level timing remains later work)
- `/song/climax`

Implementation:

- Resolve quality/effect candidates.
- Preserve URL response shapes consumed by `src/renderer/stores/player.ts`.
- Decode or pass through lyric payload in the same shape expected by current lyric store.

Acceptance:

- Existing Electron frontend can search, click a song, resolve URL, discover quality candidates, and load LRC lyric through `EchoCompatServer`.
- Failed URL resolution returns a compatible error.

## Phase 4: Playlist And Ranking

Routes:

- `/playlist/recommend`
- `/playlist/detail` (blocked on Android-encrypted `pubsongs.kugou.com/v3/get_list_info`; do not fake metadata)
- `/playlist/track/all` (first slice implemented)
- `/playlist/track/all/new` (first slice implemented, maps `listid` to the same special-song endpoint)
- `/rank/list` (first slice implemented)
- `/rank/top`
- `/rank/audio` (first slice implemented)
- `/playlist/tags` (first slice implemented)
- `/top/playlist` (first slice implemented)

Implementation:

- Page responses without keeping all songs in memory.
- Cache playlist metadata separately from track pages.
- Preserve IDs used by favorite and playback code: `id`, `hash`, `mixSongId`, `fileId`, `albumId`.

Acceptance:

- Existing Electron Explore-style playlist lists can load tags and paged playlist cards; playlist detail metadata still needs Android-encrypted support.
- Existing Electron playlist detail page can page through tracks.
- Large playlist memory stays bounded.

## Phase 5: User And Login

Routes:

- `/login/qr/key`
- `/login/qr/create`
- `/login/qr/check`
- `/captcha/sent`
- `/login/cellphone`
- `/login/wx/create`
- `/login/wx/check`
- `/login/openplat`
- `/user/detail`
- `/user/vip/detail`
- `/user/playlist`
- `/user/history`
- `/playhistory/upload`
- `/user/cloud`
- `/user/cloud/url`

Implementation:

- Persist token/session fields in `SessionRepository`.
- Preserve Authorization header compatibility.
- Emit session-expired compatible responses.

Acceptance:

- Existing Electron login page can complete at least one login flow.
- User detail and playlist routes work after login.

## Phase 6: Content Details

Routes:

- `/album/detail` (first slice implemented)
- `/album/songs` (first slice implemented)
- `/artist/detail` (first slice implemented)
- `/artist/audios` (first slice implemented)
- `/artist/albums` (first slice implemented)
- `/comment/music`
- `/comment/music/classify`
- `/comment/music/hotword`
- `/comment/playlist`
- `/comment/album`
- `/comment/floor`
- `/comment/count`
- `/favorite/count`
- `/video/url`
- `/kmr/audio/mv`
- `/video/privilege`
- `/video/detail`

Acceptance:

- Detail, comment, and MV pages can load through `EchoCompatServer`.
- Album and artist detail pages can load metadata and paged lists through `EchoCompatServer`; comment and MV routes remain later work.

## Phase 7: Native Playback

Work:

- Replace playback state stub with Media Foundation media session.
- Support open URL, play, pause, stop, seek, duration, progress, end, and error events.
- Add output device enumeration and selection.
- Add volume and playback-rate control.

Acceptance:

- `EchoPlayback` can play a known HTTP audio URL.
- 100 open/stop cycles do not leak state.
- Playback state events can be consumed by Win32 UI.

## TDD Slices

- `AuthorizationHeader_ParsesCurrentRendererFormat`
- `RegisterDevice_PersistsStableDevice`
- `ServerNow_ReturnsSecondsAndMilliseconds`
- `KnownUnportedRoute_ReturnsNativeNotImplemented`
- `SearchDefault_MatchesNodeFixture`
- `SearchSong_NormalizesKugouMobileResponse`
- `SearchHot_ReturnsRendererKeywordGroups`
- `SearchSuggest_ReturnsRendererRecordDatas`
- `SearchTypedResults_ReturnRendererAlbumArtistPlaylistCards`
- `SongUrl_PreservesRendererResponseShape`
- `PrivilegeLite_ReturnsRelateGoodsForRenderer`
- `Lyric_DecodesExpectedPayload`
- `PlaylistTracks_PaginatesWithoutFullRetention`
- `PlaylistTags_ReturnsRendererCategoryTree`
- `TopPlaylist_ReturnsPagedPlaylistCards`
- `CatalogAlbumDetail_ReturnsRendererMeta`
- `CatalogAlbumSongs_ReturnsPagedSongs`
- `CatalogArtistDetail_ReturnsRendererMeta`
- `CatalogArtistSongs_ReturnsPagedSongs`
- `CatalogArtistAlbums_ReturnsPagedAlbums`
- `RankList_ReturnsRenderableRankMeta`
- `RankAudio_ReturnsRenderableSongs`
- `PlaybackController_StateTransitionsAreStable`
