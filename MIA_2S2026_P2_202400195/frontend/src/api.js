// En AWS se compila con VITE_API_URL=http://IP-EC2:8080 npm run build
export const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8080'

async function request(path, options) {
  const res = await fetch(API_URL + path, options)
  return res.json()
}

const post = (path, body) =>
  request(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) })

const q = (params) => '?' + new URLSearchParams(params).toString()

export const api = {
  execute: (commands) => post('/execute', { commands }),
  login: (id, user, pass) => post('/login', { id, user, pass }),
  logout: () => post('/logout'),
  session: () => request('/session'),
  disks: () => request('/disks'),
  ls: (id, path) => request('/ls' + q({ id, path })),
  file: (id, path) => request('/file' + q({ id, path })),
  journaling: (id) => request('/journaling' + q({ id })),
  bitmaps: (id) => request('/bitmaps' + q({ id })),
  reports: () => request('/reports'),
  reportUrl: (path, t) => API_URL + '/report' + q({ path, t }),
}

export function formatBytes(n) {
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(2) + ' MB'
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB'
  return n + ' B'
}
