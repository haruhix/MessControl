/* Educational diagrams only. This does not control Unreal or implement a plugin. */
(() => {
  'use strict';
  const root=document.getElementById('patterns');
  if(!root)return;
  const q=s=>root.querySelector(s);
  const patterns={
    attached:{title:'Эффект следует за персонажем',read:'Источник эффекта прикреплён к компоненту или сокету. Это удобно для пузырьков у сумки, пены у рук или акцента возле зубика.',binding:'Наш зубик → выбранный компонент / socket → Niagara Component. Само прикрепление не распределяет частицы по коже.',controls:'Смещение, поворот, цвет, количество и время жизни. Отдельно решаем: старые частицы тоже следуют за персонажем или остаются в мире.',life:'Начало события → работа рядом с персонажем → остановка спавна и затухание. При смерти/замене владельца компонент освобождается.',example:'Лечение зубика: небольшой локальный пенный акцент, который не закрывает глаза.',boundary:'Прикрепить источник и приклеить каждую частицу к деформирующейся поверхности — разные задачи.',phases:['Начало','Следует за зубиком','Затухает']},
    surface:{title:'Частицы на поверхности меша',read:'Niagara выбирает точки на геометрии зубика. Можно создать блики эмали или пузырьки именно на теле, а не в случайной сфере рядом.',binding:'Конкретный Skeletal Mesh Component → Skeletal Mesh Location / sampling. Тело, глаза и одежда могут быть разными компонентами.',controls:'Частота, отступ от поверхности, допустимая область/маска, цвет, lifetime и режим следования.',life:'Выбрали точку поверхности → породили частицу. Если она должна следовать анимации, её позицию обновляем семплированием и после рождения.',example:'Короткие чистые блики по эмали после восстановления здоровья.',boundary:'Одно семплирование в Particle Spawn сохраняет позу рождения. Для следования деформации нужен Update/reproduction. Требования CPU/GPU доступа проверяем на нашем меше.',phases:['Выбор поверхности','Обновление на меше','Затухание']},
    overlay:{title:'Материал поверх меша',read:'Меняется вид самой поверхности: мягкая подсветка эмали, полоса восстановления или растворение. Это материал, а не россыпь частиц.',binding:'Материал/overlay на выбранном Mesh Component. Niagara может добавлять отдельные пузырьки, но не заменяет этот слой.',controls:'Маска, интенсивность, цвет, ширина границы и кривая появления. Материал и порядок слоёв проверяем с исходным внешним видом.',life:'Появление слоя → короткий акцент → возврат к обычной поверхности. Не оставляем постоянный свет после события.',example:'При лечении по телу зубика проходит сдержанная полоса чистой эмали.',boundary:'Overlay и sampling поверхности не взаимозаменяемы. Материалы, косметика и лицо должны корректно сосуществовать.',phases:['Появление','Акцент на эмали','Возврат материала']},
    projectile:{title:'Снаряд: запуск → полёт → попадание',read:'Это несколько визуальных ролей одного действия: акцент выстрела, оболочка летящего объекта, хвост и эффект удара.',binding:'Игровой projectile actor управляет траекторией и столкновением; Niagara получает его положение/скорость и событие попадания.',controls:'Размер оболочки, цвет, длина/плотность хвоста, акцент запуска и форма попадания. Скорость/урон — игровые параметры, не декоративные числа.',life:'Запуск → сопровождение в полёте → hit или промах → хвост доживает. При удалении снаряда освобождаем его эффекты.',example:'Ракетница: вспышка у дула, ракета с коротким хвостом, затем взрыв в настоящей точке попадания.',boundary:'Визуальная частица не должна сама подтверждать игровой урон. Готовый красивый NS ещё не является работающей ракетой.',phases:['Запуск','Полёт','Попадание']},
    impact:{title:'Одноразовое попадание',read:'Короткий акцент сообщает о контакте: главная форма, мелкие брызги и быстрый уход. Его направление связано с поверхностью.',binding:'Событие контакта → мировая точка и нормаль поверхности → one-shot Niagara.',controls:'Радиус картинки, сила/контраст, направление, число частиц и длительность. Размер VFX не меняет подтверждённый урон.',life:'Резкое появление → читаемая главная форма → затухание до полного завершения системы.',example:'Крюк коснулся цели: небольшой пенный щелчок именно в точке контакта.',boundary:'Проверяем и удачное попадание, и промах; эффект успеха не появляется просто по нажатию кнопки.',phases:['Контакт','Основная форма','Затухание']},
    trail:{title:'След остаётся в мире',read:'Источник движется, а уже созданные частицы остаются на прежнем месте. Так виден путь, а не только пятно под текущим положением.',binding:'Движущийся источник → точки истории в world space. Для ровного заполнения учитываем расстояние, а не только частоту кадров.',controls:'Ширина, интервал между отметками, lifetime, прозрачность и кривая ухода. При телепорте историю разрываем.',life:'Начали движение/контакт → пополняем историю → прекратили спавн → старый след исчезает своим lifetime.',example:'Мыльный след губки. Он появляется на поддерживаемой поверхности и не рисует мост через прыжок или телепорт.',boundary:'Ribbon может подходить для ленты оружия, а наземная пена — для отдельных карточек. Один тип renderer не решает все следы.',phases:['Начало пути','История в мире','Старый след исчезает']},
    beam:{title:'Связь между двумя точками',read:'Трос или поток связывает начало и конец. Он отличается от истории следа: обе точки могут двигаться прямо сейчас.',binding:'Начальная точка → конечная точка → beam/ribbon/mesh. Для крюка это реальные положения руки/крепления и крюка.',controls:'Толщина, натяжение/кривая, шум, цвет и акцент контакта. Локальные оси и ориентацию проверяем на поворотах.',life:'Выпуск → обновление двух концов → контакт/возврат → исчезновение после recovery.',example:'Трос крюка: рука и сам крюк не отрываются от концов линии при движении.',boundary:'Обновлять только длину недостаточно: нужны обе мировые точки. Притягивание персонажа остаётся игровой механикой.',phases:['Выпуск','Две живые точки','Возврат']},
    area:{title:'Область на земле / предупреждение',read:'Форма показывает, где будет действовать событие. Красота вторична по отношению к точности обещанной границы.',binding:'Игровой центр, радиус/форма и тайминг → визуальный телеграф, ориентированный по поверхности.',controls:'Контраст, мягкость края, рисунок и пульсация. Радиус предупреждения берём из той же игровой настройки, что и реальную область.',life:'Предупреждение → понятный момент срабатывания → уход. Остаточная декоративная пена не выдаётся за продолжающийся урон.',example:'Граната: зона опасности до взрыва. Защитную область показываем отличимым сигналом.',boundary:'Свободное художественное изменение Radius у предупреждения может обмануть игрока. Форму проверяем в реальной камере и на склонах.',phases:['Предупреждение','Срабатывание','Уход']}
  };
  const buttons=Array.from(root.querySelectorAll('[data-pattern]'));
  const slider=q('#pattern-time');
  let current='attached';
  const tooth=(x,y,scale=1)=>`<g transform="translate(${x} ${y}) scale(${scale})"><path d="M-24-38C-39-33-32-12-28 0L-23 29Q-16 45-8 23Q0 5 8 23Q16 45 23 29L28 0C32-12 39-33 24-38Q11-47 0-38Q-11-47-24-38Z" fill="var(--white)" stroke="currentColor" stroke-width="2"/><circle cx="-10" cy="-13" r="3" fill="currentColor"/><circle cx="10" cy="-13" r="3" fill="currentColor"/><path d="M-7-2Q0 5 7-2" fill="none" stroke="currentColor" stroke-width="2"/></g>`;
  const dots=(points,opacity=.8)=>points.map(([x,y,r=5])=>`<circle cx="${x}" cy="${y}" r="${r}" fill="var(--deep)" opacity="${opacity}"/>`).join('');
  const burst=(x,y,size=1,opacity=1)=>`<g transform="translate(${x} ${y}) scale(${size})" opacity="${opacity}" fill="none" stroke="var(--deep)" stroke-width="3">${Array.from({length:8},(_,i)=>{const a=i*Math.PI/4;return `<path d="M${Math.cos(a)*12} ${Math.sin(a)*12}L${Math.cos(a)*31} ${Math.sin(a)*31}"/>`;}).join('')}<circle r="9"/></g>`;
  function diagram(key,t){
    const phase=t<.34?0:t<.75?1:2;
    const x=105+t*235;
    const alpha=phase===2?Math.max(0,(1-t)*4):.8;
    let body='';
    if(key==='attached')body=tooth(x,112)+`<g transform="translate(${x} 75)">${dots([[-43,0,8],[-38,-23,5],[40,-7,6],[29,-34,4]],alpha)}</g><path d="M90 166H350" stroke="var(--line)" stroke-width="2"/>`;
    if(key==='surface'){
      const center=225+Math.sin(t*Math.PI*2)*24,stretch=1+.08*Math.sin(t*Math.PI*4);
      // Both geometry and sampled points share the same animated transform.
      body=`<g transform="translate(${center} 112) scale(1.3 ${1.3*stretch})">${tooth(0,0)}${dots([[-26,-21,3],[26,-21,3],[-16,11,3],[16,11,3],[0,-30,3]],alpha)}</g>`;
    }
    if(key==='overlay')body=tooth(225,112,1.3)+`<path d="M186 ${141-t*63}Q225 ${153-t*63} 264 ${141-t*63}" stroke="var(--deep)" stroke-width="13" stroke-linecap="round" opacity="${alpha*.6}"/>`;
    if(key==='projectile'){
      const travel=Math.max(0,Math.min(1,(t-.34)/(.75-.34))),head=123+(400-123)*travel;
      body=tooth(72,119,.75)+`<path d="M123 121H401" stroke="var(--line)" stroke-width="2" stroke-dasharray="5 6"/>`+(phase===2?burst(400,121,alpha*1.2,alpha):`<g data-projectile-x="${head}"><path d="M${head-15} 112H${head+13}V130H${head-15}Z" fill="var(--deep)"/>${travel>0?`<path d="M${head-44} 121H${head-20}" stroke="var(--deep)" stroke-width="5" opacity=".35"/>`:''}</g>`);
    }
    if(key==='impact')body=`<path d="M330 64V176" stroke="currentColor" stroke-width="4"/>${burst(319,119,t===1?0:Math.max(0,Math.sin(t*Math.PI))*1.3,t===1?0:1)}`;
    if(key==='trail'){const points=Array.from({length:8},(_,i)=>[98+i*32,154,6]).filter(p=>p[0]<x-20);body=`<path d="M75 167H390" stroke="var(--line)" stroke-width="2"/>`+dots(points,alpha*.65)+tooth(x,105,.7);}
    if(key==='beam'){
      const end=t<.34?106+64*t/.34:t<.75?170+210*(t-.34)/.41:380-274*(t-.75)/.25;
      body=tooth(80,122,.75)+`<g data-hook-x="${end}" opacity="${alpha}"><path d="M106 116Q${(106+end)/2} ${146-Math.sin(t*Math.PI)*30} ${end} 100" fill="none" stroke="var(--deep)" stroke-width="4"/><path d="M${end} 100q18-25 30-5q4 19-13 19" fill="none" stroke="var(--deep)" stroke-width="5" stroke-linecap="round"/></g>`;
    }
    if(key==='area')body=`<ellipse cx="230" cy="140" rx="115" ry="39" fill="var(--mint)" stroke="var(--deep)" stroke-width="2" opacity="${alpha}"/><ellipse cx="230" cy="140" rx="${25+t*80}" ry="${9+t*25}" fill="none" stroke="var(--deep)" stroke-width="2" opacity="${alpha*.65}"/>${phase===1?burst(230,115,.65):''}`;
    return `<svg class="pattern-diagram" viewBox="0 0 460 220" role="img" aria-label="${patterns[key].title}: ${patterns[key].phases[phase]}. Учебная схема."><text x="18" y="26" fill="currentColor" font-size="11" letter-spacing="1">СХЕМА, НЕ NIAGARA PREVIEW</text>${body}<text x="230" y="203" text-anchor="middle" fill="currentColor" font-size="13">${patterns[key].phases[phase]}</text></svg>`;
  }
  function render(){
    const p=patterns[current],t=Number(slider.value)/100;
    q('#pattern-name').textContent=p.title;
    q('#pattern-read').textContent=p.read;
    ['binding','controls','life','example','boundary'].forEach(k=>q('#pattern-'+k).textContent=p[k]);
    q('#pattern-diagram-host').innerHTML=diagram(current,t);
    q('#pattern-time-value').textContent=slider.value+'%';
    q('#pattern-phase').textContent=p.phases[t<.34?0:t<.75?1:2];
    buttons.forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.pattern===current)));
  }
  buttons.forEach(b=>b.addEventListener('click',()=>{current=b.dataset.pattern;render();}));
  slider.addEventListener('input',render);
  render();
})();
