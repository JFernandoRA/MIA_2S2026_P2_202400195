import { useState, useRef } from 'react'
import { api, API_URL } from '../api'

const PLACEHOLDER = `mkdisk -size=10 -unit=M -path=/home/fercho/discos/Disco1.mia
fdisk -size=3 -unit=M -path=/home/fercho/discos/Disco1.mia -name=Part1
mount -path=/home/fercho/discos/Disco1.mia -name=Part1
mkfs -id=951A -fs=3fs`

// Busca los "fdisk ... -delete" del script para pedir confirmación antes de ejecutar
function findDeletes(text) {
  return text
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => !l.startsWith('#') && /^fdisk\b/i.test(l) && /-delete\s*=/i.test(l))
}

export default function Console({ hidden, onExecuted, connected, setConnected }) {
  const [input, setInput] = useState('')
  const [output, setOutput] = useState('')
  const [running, setRunning] = useState(false)
  const [fileName, setFileName] = useState('')
  const [stats, setStats] = useState(null)
  const fileRef = useRef(null)

  async function execute() {
    if (!input.trim()) return
    const deletes = findDeletes(input)
    if (deletes.length && !window.confirm(`Se eliminarán particiones:\n\n${deletes.join('\n')}\n\n¿Desea continuar?`)) return
    setRunning(true)
    try {
      const data = await api.execute(input)
      setConnected(true)
      setOutput((prev) => (prev ? prev + '\n' : '') + (data.output || data.error || ''))
      if (typeof data.total === 'number') setStats({ ok: data.ok, errors: data.errors, total: data.total })
      onExecuted()
    } catch {
      setConnected(false)
      setStats(null)
      setOutput((prev) => (prev ? prev + '\n' : '') + `# no se pudo conectar con ${API_URL}\n`)
    } finally {
      setRunning(false)
    }
  }

  function clear() {
    setInput('')
    setOutput('')
    setFileName('')
    setStats(null)
  }

  function loadFile(e) {
    const file = e.target.files[0]
    if (!file) return
    setFileName(file.name)
    const reader = new FileReader()
    reader.onload = (ev) => setInput(ev.target.result)
    reader.readAsText(file)
    e.target.value = ''
  }

  return (
    <main className="layout" hidden={hidden}>
      <section className="console">
        <div className="pane">
          <div className="pane-head">
            <span>entrada</span>
            <div className="pane-actions">
              <button className="btn" onClick={() => fileRef.current?.click()}>elegir archivo .smia</button>
              <input ref={fileRef} type="file" accept=".smia,.txt" onChange={loadFile} style={{ display: 'none' }} />
              {fileName && <span className="filename">{fileName}</span>}
            </div>
          </div>
          <textarea
            className="editor"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
                e.preventDefault()
                execute()
              }
            }}
            placeholder={PLACEHOLDER}
            spellCheck={false}
          />
        </div>

        <div className="toolbar">
          <button className="btn btn-primary" onClick={execute} disabled={running}>
            {running ? 'ejecutando…' : 'ejecutar'}
          </button>
          <button className="btn" onClick={clear}>limpiar</button>
          <span className="hint">ctrl/cmd + enter para ejecutar</span>
        </div>

        <div className="pane">
          <div className="pane-head">
            <span>salida</span>
            {stats && (
              <div className="stats">
                <span className="stat stat-ok">{stats.ok} exitosos</span>
                <span className="stat stat-err">{stats.errors} con error</span>
                <span className="stat">{stats.total} en total</span>
              </div>
            )}
          </div>
          <pre className="editor output">{output || '# los resultados de tus comandos aparecerán aquí'}</pre>
        </div>
      </section>

      <aside className="sidebar">
        <div className="sidebar-block">
          <h2>referencia rápida</h2>
          <dl className="cmd-list">
            <dt>mkdisk</dt><dd>-size -unit -path -fit</dd>
            <dt>fdisk</dt><dd>-size -path -name -type -unit -fit -delete -add</dd>
            <dt>mount</dt><dd>-path -name</dd>
            <dt>unmount</dt><dd>-id</dd>
            <dt>mkfs</dt><dd>-id -type -fs=2fs|3fs</dd>
            <dt>mkdir</dt><dd>-path -p</dd>
            <dt>mkfile</dt><dd>-path -r -size -cont</dd>
            <dt>remove</dt><dd>-path</dd>
            <dt>rename</dt><dd>-path -name</dd>
            <dt>copy</dt><dd>-path -destino</dd>
            <dt>move</dt><dd>-path -destino</dd>
            <dt>find</dt><dd>-path -name (? *)</dd>
            <dt>chown</dt><dd>-path -usuario -r</dd>
            <dt>chmod</dt><dd>-path -ugo -r</dd>
            <dt>loss</dt><dd>-id</dd>
            <dt>journaling</dt><dd>-id</dd>
            <dt>rep</dt><dd>-name -path -id -path_file_ls</dd>
          </dl>
        </div>
        <div className="sidebar-block">
          <h2>notas</h2>
          <p className="note">El inicio de sesión se hace con el botón <code>iniciar sesión</code>. Backend: <code>{API_URL}</code> {connected === false && '(sin conexión)'}</p>
        </div>
      </aside>
    </main>
  )
}
