import { useState, useEffect, useCallback } from 'react'
import './App.css'
import { api } from './api'
import { DiskMark } from './components/Icons'
import Console from './components/Console'
import Login from './components/Login'
import Explorer from './components/Explorer'
import Journal from './components/Journal'
import Loss from './components/Loss'
import Reports from './components/Reports'

const TABS = [
  { key: 'consola', label: 'consola' },
  { key: 'explorador', label: 'visualizador', needsLogin: true },
  { key: 'journaling', label: 'journaling' },
  { key: 'loss', label: 'loss' },
  { key: 'reportes', label: 'reportes' },
]

function App() {
  const [view, setView] = useState('consola')
  const [session, setSession] = useState({ active: false })
  const [connected, setConnected] = useState(null)
  const [refreshKey, setRefreshKey] = useState(0)

  const refreshSession = useCallback(async () => {
    try {
      const s = await api.session()
      setSession(s)
      setConnected(true)
      return s
    } catch {
      setConnected(false)
      return { active: false }
    }
  }, [])

  useEffect(() => { refreshSession() }, [refreshSession])

  // Tras ejecutar comandos: la sesión pudo cambiar (unmount) y el visualizador debe refrescarse
  async function handleExecuted() {
    const s = await refreshSession()
    setRefreshKey((k) => k + 1)
    if (!s.active && view === 'explorador') setView('consola')
  }

  async function logout() {
    await api.logout().catch(() => {})
    await refreshSession()
    setView('consola')
  }

  function openTab(t) {
    setView(t.needsLogin && !session.active ? 'login' : t.key)
  }

  return (
    <div className="shell">
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark"><DiskMark /></span>
          <span className="brand-name">ExtreamFS</span>
          <span className="brand-sub">online</span>
        </div>
        <nav className="tabs">
          {TABS.map((t) => (
            <button key={t.key} className={`tab ${view === t.key ? 'is-active' : ''}`} onClick={() => openTab(t)}>{t.label}</button>
          ))}
        </nav>
        <div className="session-box">
          <span className={`status status-${connected === null ? 'unknown' : connected ? 'ok' : 'down'}`}>
            <span className="status-dot" />
            {connected === null ? 'sin verificar' : connected ? 'conectado' : 'sin conexión'}
          </span>
          {session.active ? (
            <>
              <span className="session-user">{session.user}<span className="dim"> @ {session.id}</span></span>
              <button className="btn btn-danger" onClick={logout}>cerrar sesión</button>
            </>
          ) : (
            <button className="btn btn-login" onClick={() => setView('login')}>iniciar sesión</button>
          )}
        </div>
      </header>

      <Console hidden={view !== 'consola'} onExecuted={handleExecuted} connected={connected} setConnected={setConnected} />
      {view === 'login' && (
        <Login
          onSuccess={(s) => { setSession(s); setRefreshKey((k) => k + 1); setView('explorador') }}
          onCancel={() => setView('consola')}
        />
      )}
      {session.active && <Explorer hidden={view !== 'explorador'} refreshKey={refreshKey} />}
      {view === 'journaling' && <Journal refreshKey={refreshKey} defaultId={session.active ? session.id : ''} />}
      {view === 'loss' && <Loss refreshKey={refreshKey} onExecuted={handleExecuted} />}
      {view === 'reportes' && <Reports refreshKey={refreshKey} />}
    </div>
  )
}

export default App
