export class ApiError extends Error {
  constructor(message, status, retryAfter = 0) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.retryAfter = retryAfter;
  }
}

export function readToken() {
  try {
    return localStorage.getItem("token");
  } catch {
    return null;
  }
}

export function saveToken(token) {
  try {
    if (token) localStorage.setItem("token", token);
    else localStorage.removeItem("token");
  } catch {
    throw new Error("Разрешите хранение данных сайта для входа.");
  }
}

export function queryString(values) {
  const query = new URLSearchParams();
  for (const [key, value] of Object.entries(values)) {
    if (value !== undefined && value !== null) query.set(key, String(value));
  }
  return query.toString();
}

export async function request(
  path,
  { signal, method = "GET", body, headers, ...options } = {},
) {
  const token = readToken();
  const controller = new AbortController();
  let timedOut = false;
  const cancel = () => controller.abort(signal?.reason);
  if (signal?.aborted) cancel();
  else signal?.addEventListener("abort", cancel, { once: true });
  const timer = setTimeout(() => {
    timedOut = true;
    controller.abort();
  }, 20000);
  let response;
  let text;
  try {
    response = await fetch(`/api${path}`, {
      ...options,
      method,
      signal: controller.signal,
      credentials: "same-origin",
      headers: {
        Accept: "application/json",
        ...(body !== undefined ? { "Content-Type": "application/json" } : {}),
        ...(token ? { Authorization: `Bearer ${token}` } : {}),
        ...headers,
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    text = await response.text();
  } catch (error) {
    if (signal?.aborted) throw error;
    throw new ApiError(
      timedOut
        ? "Сервер отвечает слишком долго. Попробуйте ещё раз."
        : "Не удалось подключиться. Проверьте соединение.",
      0,
    );
  } finally {
    clearTimeout(timer);
    signal?.removeEventListener("abort", cancel);
  }
  let data;
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    throw new ApiError("Сервер вернул некорректный ответ.", response.status);
  }
  if (!response.ok) {
    throw new ApiError(
      data.error || data.message || `Ошибка запроса (${response.status})`,
      response.status,
      Number(response.headers.get("retry-after")) || 0,
    );
  }
  return data;
}
