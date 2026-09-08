(() => {
  'use strict';

  const STORAGE_KEY = 'browser-capability-fixture-marker';
  const COOKIE_NAME = 'browser_capability_fixture_marker';
  const DATABASE_NAME = 'browser-capability-fixture';
  const STORE_NAME = 'markers';

  const localValue = document.querySelector('#local-value');
  const cookieValue = document.querySelector('#cookie-value');
  const indexedDbValue = document.querySelector('#indexeddb-value');
  const storageStatus = document.querySelector('#storage-status');
  const markerSelect = document.querySelector('#marker');

  function setStorageStatus(message) {
    storageStatus.textContent = message;
  }

  function readCookie() {
    const prefix = `${COOKIE_NAME}=`;
    const found = document.cookie.split('; ').find((item) => item.startsWith(prefix));
    return found ? decodeURIComponent(found.slice(prefix.length)) : null;
  }

  function openDatabase() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DATABASE_NAME, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(STORE_NAME);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }

  async function readDatabase() {
    const database = await openDatabase();
    return new Promise((resolve, reject) => {
      const transaction = database.transaction(STORE_NAME, 'readonly');
      const request = transaction.objectStore(STORE_NAME).get('current');
      let value = null;
      request.onsuccess = () => { value = request.result || null; };
      request.onerror = () => {
        database.close();
        reject(request.error);
      };
      transaction.oncomplete = () => {
        database.close();
        resolve(value);
      };
      transaction.onerror = () => {
        database.close();
        reject(transaction.error);
      };
      transaction.onabort = () => {
        database.close();
        reject(transaction.error || new Error('Storage transaction aborted'));
      };
    });
  }

  async function refreshValues() {
    localValue.textContent = localStorage.getItem(STORAGE_KEY) || 'Not stored';
    cookieValue.textContent = readCookie() || 'Not stored';
    try {
      indexedDbValue.textContent = (await readDatabase()) || 'Not stored';
    } catch (error) {
      indexedDbValue.textContent = 'Unavailable';
    }
  }

  async function storeMarker(marker) {
    localStorage.setItem(STORAGE_KEY, marker);
    document.cookie = `${COOKIE_NAME}=${encodeURIComponent(marker)}; Path=/; SameSite=Lax`;
    const database = await openDatabase();
    try {
      await new Promise((resolve, reject) => {
        const transaction = database.transaction(STORE_NAME, 'readwrite');
        const request = transaction.objectStore(STORE_NAME).put(marker, 'current');
        request.onerror = () => reject(request.error);
        transaction.oncomplete = resolve;
        transaction.onerror = () => reject(transaction.error);
        transaction.onabort = () => reject(transaction.error || new Error('Storage transaction aborted'));
      });
    } finally {
      database.close();
    }
    await refreshValues();
  }

  function deleteDatabase() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.deleteDatabase(DATABASE_NAME);
      request.onsuccess = resolve;
      request.onerror = () => reject(request.error);
      request.onblocked = () => reject(new Error('Database deletion is blocked'));
    });
  }

  document.querySelector('#marker-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    try {
      await storeMarker(markerSelect.value);
      setStorageStatus('Marker stored.');
    } catch (error) {
      setStorageStatus(`Storage error: ${error.message}`);
    }
  });

  document.querySelector('#clear-storage').addEventListener('click', async () => {
    try {
      localStorage.removeItem(STORAGE_KEY);
      document.cookie = `${COOKIE_NAME}=; Max-Age=0; Path=/; SameSite=Lax`;
      await deleteDatabase();
      await refreshValues();
      setStorageStatus('Fixture storage cleared.');
    } catch (error) {
      setStorageStatus(`Storage error: ${error.message}`);
    }
  });

  document.querySelector('#request-location').addEventListener('click', () => {
    const status = document.querySelector('#location-status');
    if (!navigator.geolocation) {
      status.textContent = 'Geolocation is unavailable.';
      return;
    }
    status.textContent = 'Requesting permission…';
    navigator.geolocation.getCurrentPosition(
      () => { status.textContent = 'Permission granted.'; },
      (error) => { status.textContent = `Permission result: ${error.message}`; },
      { maximumAge: 0, timeout: 10000 }
    );
  });

  const video = document.querySelector('#pattern-video');
  const videoStatus = document.querySelector('#video-status');
  video.addEventListener('loadedmetadata', () => {
    videoStatus.textContent = 'Video loaded.';
  });
  video.addEventListener('playing', () => {
    videoStatus.textContent = 'Video playing.';
  });
  video.addEventListener('pause', () => {
    if (!video.ended) videoStatus.textContent = 'Video paused.';
  });
  video.addEventListener('ended', () => {
    videoStatus.textContent = 'Video ended.';
  });
  video.addEventListener('error', () => {
    videoStatus.textContent = 'Video could not be loaded or played.';
  });
  if (video.readyState >= video.HAVE_METADATA) {
    videoStatus.textContent = 'Video loaded.';
  }

  refreshValues().catch((error) => setStorageStatus(`Storage error: ${error.message}`));
})();
