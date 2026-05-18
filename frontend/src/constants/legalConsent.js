// sync with backend kLegalConsentVersion
export const LEGAL_CONSENT_VERSION = '2026-03-20';

export const OAUTH_LEGAL_STORAGE_KEY = 'clipstream_oauth_legal_consent';

export const LEGAL_CONSENT_LOCAL_ACK_KEY = 'clipstream_legal_consent_ack';

export function shouldShowLegalConsentModal() {
  try {
    return localStorage.getItem(LEGAL_CONSENT_LOCAL_ACK_KEY) !== LEGAL_CONSENT_VERSION;
  } catch {
    return true;
  }
}

export function rememberLegalConsentAck() {
  try {
    localStorage.setItem(LEGAL_CONSENT_LOCAL_ACK_KEY, LEGAL_CONSENT_VERSION);
  } catch {
    /* storage недоступен — модалка будет показываться чаще */
  }
}

export function forgetLegalConsentAck() {
  try {
    localStorage.removeItem(LEGAL_CONSENT_LOCAL_ACK_KEY);
  } catch {
    /* ignore */
  }
}
