import { useState, useEffect } from 'react'
import { api } from '../api'

const REMEMBER_KEY = 'extreamfs.login'

function loadRemembered() {
  try {
    return JSON.parse(localStorage.getItem(REMEMBER_KEY)) || null
  } catch {
    return null
  }
}

export default function Login({ onSuccess, onCancel }) {
  const saved = loadRemembered()
  const [id, setId] = useState(saved?.id || '')
  const [user, setUser] = useState(saved?.user || '')
  const [pass, setPass] = useState('')
  const [remember, setRemember] = useState(!!saved)
  const [mountedIds, setMountedIds] = useState([])
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    api.disks().then((d) => {
      const ids = []
      d.disks.forEach((disk) => disk.partitions.forEach((p) => p.id && p.fs !== 'sin formato' && ids.push(p.id)))
      setMountedIds(ids)
    }).catch(() => {})
  }, [])

  async function submit(e) {
    e.preventDefault()
    setError('')
    if (!id.trim() || !user.trim() || !pass) return setError('Complete los tres campos')
    setBusy(true)
    try {
      const r = await api.login(id.trim(), user.trim(), pass)
      if (!r.ok) return setError(r.message.replace(/^LOGIN:\s*/, ''))
      try {
        if (remember) localStorage.setItem(REMEMBER_KEY, JSON.stringify({ id: id.trim(), user: user.trim() }))
        else localStorage.removeItem(REMEMBER_KEY)
      } catch { /* almacenamiento no disponible */ }
      onSuccess(r.session)
    } catch {
      setError('No se pudo conectar con el backend')
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="center-view">
      <form className="card login-card" onSubmit={submit}>
        <h1 className="card-title">Iniciar sesión</h1>
        <label className="field">
          <span>ID partición</span>
          <input list="mounted-ids" value={id} onChange={(e) => setId(e.target.value)} placeholder="951A" autoFocus />
          <datalist id="mounted-ids">
            {mountedIds.map((m) => <option key={m} value={m} />)}
          </datalist>
        </label>
        <label className="field">
          <span>Usuario</span>
          <input value={user} onChange={(e) => setUser(e.target.value)} placeholder="root" autoComplete="username" />
        </label>
        <label className="field">
          <span>Contraseña</span>
          <input type="password" value={pass} onChange={(e) => setPass(e.target.value)} autoComplete="current-password" />
        </label>
        <label className="check">
          <input type="checkbox" checked={remember} onChange={(e) => setRemember(e.target.checked)} />
          Recordar usuario
        </label>
        {error && <p className="form-error">{error}</p>}
        <div className="form-actions">
          <button className="btn btn-primary" type="submit" disabled={busy}>{busy ? 'entrando…' : 'entrar'}</button>
          <button className="btn" type="button" onClick={onCancel}>cancelar</button>
        </div>
        {mountedIds.length === 0 && <p className="note">No hay particiones montadas y formateadas. Use mount y mkfs en la consola.</p>}
      </form>
    </div>
  )
}
